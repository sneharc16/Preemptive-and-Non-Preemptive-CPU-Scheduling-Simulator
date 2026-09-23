// CPU Scheduling Simulator - browser front end. The simulation itself is the
// C engine compiled to WebAssembly (sched.js / sched.wasm): this file writes
// the workload to the module's in-memory filesystem, calls the CLI's main()
// with --format=json, and draws the result.
import createSched from "./sched.js";
import { PRESETS } from "./presets.js";

const POLICIES = [
  ["fcfs", "FCFS"], ["sjf", "SJF"], ["srtf", "SRTF"], ["rr", "Round Robin"],
  ["prio", "Priority"], ["prio-p", "Priority (preemptive)"], ["hrrn", "HRRN"], ["mlfq", "MLFQ"],
  ["lottery", "Lottery"], ["stride", "Stride"], ["cfs", "CFS-lite"], ["opt-np", "Optimal (offline)"],
];
const MAX_COLOURED = 8; // categorical slots; beyond this, segments carry text labels only
const $ = (id) => document.getElementById(id);

const state = { procs: [], result: null, time: 0, playing: false, span: [0, 1], last: 0 };
let engine = null;
let output = [];
let runTimer = 0;

// ---------------------------------------------------------------- engine

async function loadEngine() {
  engine = await createSched({
    print: (line) => output.push(line),
    printErr: (line) => output.push("\u0000" + line),
  });
}

function runEngine(csv, args) {
  output = [];
  engine.FS.writeFile("/workload.csv", csv);
  let code;
  try {
    code = engine.callMain(["--input=/workload.csv", "--format=json", ...args]);
  } catch (e) {
    return { error: String(e) };
  }
  const errors = output.filter((l) => l.startsWith("\u0000")).map((l) => l.slice(1));
  if (code !== 0) {
    return { error: errors.join(" ").replace(/^[^:]*: error: /, "") || `exit code ${code}` };
  }
  return { doc: JSON.parse(output.filter((l) => !l.startsWith("\u0000")).join("\n")) };
}

// ---------------------------------------------------------------- workload table

const FIELDS = ["pid", "arrival", "bursts", "priority", "tickets", "nice"];
const DEFAULTS = { priority: 0, tickets: 100, nice: 0 };

function setProcs(procs) {
  state.procs = procs.map(([pid, arrival, bursts, priority = 0, tickets = 100, nice = 0]) =>
    ({ pid, arrival, bursts: String(bursts), priority, tickets, nice }));
  renderProcTable();
}

function renderProcTable() {
  const body = $("procs").tBodies[0];
  body.replaceChildren();
  state.procs.forEach((p, i) => {
    const tr = document.createElement("tr");
    for (const f of FIELDS) {
      const td = document.createElement("td");
      const input = document.createElement("input");
      input.value = p[f];
      input.inputMode = f === "bursts" ? "text" : "numeric";
      input.setAttribute("aria-label", `${f} of row ${i + 1}`);
      input.addEventListener("input", () => { p[f] = input.value.trim(); scheduleRun(); });
      td.append(input);
      tr.append(td);
    }
    const td = document.createElement("td");
    const rm = document.createElement("button");
    rm.type = "button";
    rm.className = "remove ghost";
    rm.textContent = "×";
    rm.setAttribute("aria-label", `Remove row ${i + 1}`);
    rm.addEventListener("click", () => { state.procs.splice(i, 1); renderProcTable(); scheduleRun(); });
    td.append(rm);
    tr.append(td);
    body.append(tr);
  });
}

function workloadCsv() {
  const lines = ["pid,arrival,bursts,priority,tickets,nice"];
  for (const p of state.procs) {
    lines.push(FIELDS.map((f) => (p[f] === "" && f in DEFAULTS ? DEFAULTS[f] : p[f])).join(","));
  }
  return lines.join("\n") + "\n";
}

// ---------------------------------------------------------------- controls

function selectedAlgos() {
  return [...document.querySelectorAll("#algos input:checked")].map((i) => i.value);
}

function params() {
  const num = (id) => $(id).value.trim() || "0";
  return { quantum: num("quantum"), cs: num("cs"), cores: num("cores"), aging: num("aging"),
           boost: num("boost"), seed: num("seed") };
}

function applyPreset(key) {
  const pr = PRESETS[key];
  $("preset-note").textContent = pr.note;
  document.querySelectorAll("#algos input").forEach((i) => { i.checked = pr.algos.includes(i.value); });
  const p = { quantum: 2, cs: 0, cores: 1, aging: 0, boost: 0, seed: 1, ...pr.params };
  for (const k of Object.keys(p)) $(k).value = p[k];
  setProcs(pr.procs);
  run();
}

function scheduleRun() {
  clearTimeout(runTimer);
  runTimer = setTimeout(run, 250);
}

function run() {
  if (!engine) return;
  const p = params();
  let algos = selectedAlgos();
  const notes = [];
  const singleBurst = state.procs.every((x) => !String(x.bursts).includes(":"));
  const exactOk = state.procs.length <= 20 && singleBurst && p.cores === "1" && p.cs === "0";
  if (algos.includes("opt-np") && !exactOk) {
    algos = algos.filter((a) => a !== "opt-np");
    notes.push("The exact optimum needs at most 20 processes, no I/O phases, one core and no switch cost.");
  }
  if (algos.length === 0) {
    showError("Choose at least one policy.");
    return;
  }
  const args = [`--algo=${algos.join(",")}`, `--quantum=${p.quantum}`, `--cs-cost=${p.cs}`,
                `--cores=${p.cores}`, `--aging=${p.aging}`, `--mlfq-boost=${p.boost}`, `--seed=${p.seed}`];
  if (exactOk) args.push("--gap");
  const res = runEngine(workloadCsv(), args);
  if (res.error) {
    showError(res.error);
    return;
  }
  showError("");
  state.result = res.doc;
  const results = res.doc.results;
  const first = Math.min(...res.doc.workload.map((w) => w.arrival));
  const last = Math.max(...results.map((r) => r.makespan));
  state.span = [first, last];
  $("scrub").min = first;
  $("scrub").max = last;
  setTime(state.playing ? Math.min(state.time, last) : last);
  renderLegend(res.doc.workload);
  renderCharts(results);
  renderMetrics(results, res.doc, notes);
}

function showError(msg) {
  $("error").hidden = !msg;
  $("error").textContent = msg;
}

// ---------------------------------------------------------------- charts

const NS = "http://www.w3.org/2000/svg";
const svgEl = (tag, attrs = {}) => {
  const el = document.createElementNS(NS, tag);
  for (const [k, v] of Object.entries(attrs)) {
    // CSS custom properties only resolve through style, not presentation attributes
    if ((k === "fill" || k === "stroke") && String(v).startsWith("var(")) el.style[k] = v;
    else el.setAttribute(k, v);
  }
  return el;
};

function colourOf(pid, workload) {
  if (workload.length > MAX_COLOURED) return "var(--run-neutral)";
  const i = workload.findIndex((w) => w.pid === pid);
  return `var(--s${i + 1})`;
}

function renderLegend(workload) {
  const box = $("legend");
  box.replaceChildren();
  if (workload.length <= MAX_COLOURED) {
    for (const w of workload) {
      const s = document.createElement("span");
      const i = document.createElement("i");
      i.style.background = colourOf(w.pid, workload);
      s.append(i, `P${w.pid}`);
      box.append(s);
    }
  } else {
    const s = document.createElement("span");
    s.textContent = "Many processes: segments are labelled with their PID.";
    box.append(s);
  }
  for (const [cls, text] of [["cs", "context switch"], ["idle", "idle"]]) {
    const s = document.createElement("span");
    const i = document.createElement("i");
    i.className = cls;
    s.append(i, text);
    box.append(s);
  }
}

const LANE_H = 26;
const LABEL_W = 56;

function renderCharts(results) {
  const box = $("charts");
  box.replaceChildren();
  const [t0, t1] = state.span;
  const ticks = Math.max(1, t1 - t0);
  const avail = Math.max(240, box.clientWidth - LABEL_W - 8);
  const px = Math.max(3, Math.min(48, avail / ticks));
  const width = LABEL_W + ticks * px + 8;
  const workload = state.result.workload;
  const x = (t) => LABEL_W + (t - t0) * px;
  const step = niceStep(ticks, (ticks * px) / 70);

  for (const r of results) {
    const card = document.createElement("div");
    card.className = "chart";
    const h = document.createElement("h3");
    h.textContent = policyName(r.algorithm) + (r.quantum ? ` (q = ${r.quantum})` : "");
    const small = document.createElement("small");
    small.textContent = `mean turnaround ${fmt(r.averages.turnaround)} · mean response ${fmt(r.averages.response)}`;
    h.append(small);
    const scroll = document.createElement("div");
    scroll.className = "chart-scroll";
    const height = r.cores * LANE_H + 22;
    const svg = svgEl("svg", { width, height, viewBox: `0 0 ${width} ${height}`, role: "img",
      "aria-label": `Gantt chart for ${policyName(r.algorithm)}` });
    const defs = svgEl("defs");
    const pat = svgEl("pattern", { id: `hatch-${r.algorithm}`, width: 5, height: 5,
      patternUnits: "userSpaceOnUse", patternTransform: "rotate(45)" });
    pat.append(svgEl("rect", { width: 5, height: 5, fill: "var(--surface)" }),
               svgEl("line", { x1: 0, y1: 0, x2: 0, y2: 5, stroke: "var(--muted)", "stroke-width": 2 }));
    const clip = svgEl("clipPath", { id: `clip-${r.algorithm}` });
    const clipRect = svgEl("rect", { x: 0, y: 0, width: x(state.time), height });
    clip.append(clipRect);
    defs.append(pat, clip);
    svg.append(defs);

    for (let c = 0; c < r.cores; c++) {
      const y = c * LANE_H;
      svg.append(svgEl("rect", { x: LABEL_W, y: y + 3, width: ticks * px, height: LANE_H - 6,
        fill: "var(--idle)", rx: 3 }));
      const lane = svgEl("text", { x: 0, y: y + LANE_H / 2 + 4, class: "lane-text" });
      lane.textContent = r.cores > 1 ? `core ${c}` : "CPU";
      svg.append(lane);
    }
    const segs = svgEl("g", { "clip-path": `url(#clip-${r.algorithm})` });
    for (const s of r.gantt) {
      if (s.type === "idle") continue;
      const y = s.core * LANE_H + 3;
      const w = Math.max(1, (s.end - s.start) * px - 1);
      const fill = s.type === "cs" ? `url(#hatch-${r.algorithm})` : colourOf(s.pid, workload);
      const rect = svgEl("rect", { x: x(s.start) + 0.5, y, width: w, height: LANE_H - 6, rx: 3,
        fill, class: "seg" });
      rect.dataset.tip = `${s.type === "cs" ? "switch to " : ""}P${s.pid} · [${s.start}, ${s.end}) · ${s.end - s.start} tick${s.end - s.start === 1 ? "" : "s"}${r.cores > 1 ? ` · core ${s.core}` : ""}`;
      segs.append(rect);
      if (s.type === "run" && w >= 22) {
        const t = svgEl("text", { x: x(s.start) + w / 2 + 0.5, y: y + (LANE_H - 6) / 2 + 4,
          "text-anchor": "middle", class: "seg-label" });
        t.textContent = `P${s.pid}`;
        segs.append(t);
      }
    }
    svg.append(segs);
    const axisY = r.cores * LANE_H + 2;
    for (let t = Math.ceil(t0 / step) * step; t <= t1; t += step) {
      svg.append(svgEl("line", { x1: x(t), x2: x(t), y1: axisY, y2: axisY + 4, stroke: "var(--axis)" }));
      const label = svgEl("text", { x: x(t), y: axisY + 16, "text-anchor": "middle", class: "axis-text" });
      label.textContent = t;
      svg.append(label);
    }
    const head = svgEl("line", { x1: x(state.time), x2: x(state.time), y1: 0, y2: r.cores * LANE_H,
      stroke: "var(--ink-2)", "stroke-width": 1.5, class: "playhead" });
    svg.append(head);
    svg._update = (time) => {
      clipRect.setAttribute("width", x(time));
      head.setAttribute("x1", x(time));
      head.setAttribute("x2", x(time));
    };
    scroll.append(svg);
    card.append(h, scroll);
    box.append(card);
  }
}

function niceStep(range, maxLabels) {
  const raw = range / Math.max(1, maxLabels);
  const mag = 10 ** Math.floor(Math.log10(Math.max(raw, 1)));
  for (const m of [1, 2, 5, 10]) if (m * mag >= raw) return m * mag;
  return 10 * mag;
}

function setTime(t) {
  state.time = t;
  $("scrub").value = t;
  $("clock").textContent = `t = ${Math.floor(t)}`;
  document.querySelectorAll("#charts svg").forEach((svg) => svg._update && svg._update(t));
}

function frame(now) {
  if (!state.playing) return;
  const dt = state.last ? (now - state.last) / 1000 : 0;
  state.last = now;
  const next = state.time + dt * Number($("speed").value);
  if (next >= state.span[1]) {
    setTime(state.span[1]);
    togglePlay(false);
    return;
  }
  setTime(next);
  requestAnimationFrame(frame);
}

function togglePlay(on) {
  state.playing = on;
  $("play").textContent = on ? "Pause" : "Play";
  $("play").setAttribute("aria-label", on ? "Pause" : "Play");
  if (on) {
    if (state.time >= state.span[1]) setTime(state.span[0]);
    state.last = 0;
    requestAnimationFrame(frame);
  }
}

// ---------------------------------------------------------------- metrics

const policyName = (a) => (POLICIES.find(([k]) => k === a) || [a, a])[1];
const fmt = (v) => (Math.abs(v) >= 100 ? v.toFixed(1) : v.toFixed(2));

function renderMetrics(results, doc, notes) {
  const cols = [
    ["Mean turnaround", (r) => r.summary.turnaround.mean, "min"],
    ["Mean waiting", (r) => r.summary.waiting.mean, "min"],
    ["Mean response", (r) => r.summary.response.mean, "min"],
    ["p95 turnaround", (r) => r.summary.turnaround.p95, "min"],
    ["Max waiting", (r) => r.summary.waiting.max, "min"],
    ["Jain fairness", (r) => r.summary.jain_fairness_slowdown, "max"],
    ["Context switches", (r) => r.summary.context_switches, "min"],
  ];
  if (results.every((r) => "optimality_gap_pct" in r)) {
    cols.push(["Gap vs optimum (%)", (r) => r.optimality_gap_pct, "none"]);
  }
  const table = $("metrics");
  table.replaceChildren();
  const head = table.createTHead().insertRow();
  for (const name of ["Policy", ...cols.map((c) => c[0])]) {
    const th = document.createElement("th");
    th.textContent = name;
    head.append(th);
  }
  const body = table.createTBody();
  const best = cols.map(([, f, dir]) => {
    if (dir === "none") return null;
    const vals = results.map(f);
    return dir === "min" ? Math.min(...vals) : Math.max(...vals);
  });
  for (const r of results) {
    const tr = body.insertRow();
    tr.insertCell().textContent = policyName(r.algorithm);
    cols.forEach(([, f], i) => {
      const td = tr.insertCell();
      const v = f(r);
      td.textContent = Number.isInteger(v) ? v : fmt(v);
      if (best[i] !== null && results.length > 1 && v === best[i]) td.className = "best";
    });
  }
  const extra = [];
  if (doc.optimum_mean_turnaround !== undefined) {
    extra.push(`Exact non-preemptive optimum: mean turnaround ${fmt(doc.optimum_mean_turnaround)}. Preemptive policies can beat it.`);
  }
  extra.push("★ marks the best value in each column.");
  $("metrics-note").textContent = [...notes, ...extra].join(" ");
}

// ---------------------------------------------------------------- theme, tooltip, setup

function initTheme() {
  const modes = ["system", "light", "dark"];
  let mode = "system";
  try { mode = localStorage.getItem("theme") || "system"; } catch { /* storage unavailable */ }
  const asked = new URLSearchParams(location.search).get("theme"); // e.g. ?theme=light
  if (modes.includes(asked)) mode = asked;
  const apply = () => {
    if (mode === "system") document.documentElement.removeAttribute("data-theme");
    else document.documentElement.dataset.theme = mode;
    $("theme").textContent = `Theme: ${mode}`;
  };
  $("theme").addEventListener("click", () => {
    mode = modes[(modes.indexOf(mode) + 1) % modes.length];
    try { localStorage.setItem("theme", mode); } catch { /* ignore */ }
    apply();
  });
  apply();
}

function initTooltip() {
  const tip = $("tip");
  const charts = $("charts");
  const show = (e) => {
    const text = e.target.dataset && e.target.dataset.tip;
    if (!text) { tip.hidden = true; return; }
    tip.textContent = text;
    tip.hidden = false;
    const pad = 12;
    const x = Math.min(e.clientX + pad, window.innerWidth - tip.offsetWidth - 4);
    const y = e.clientY + pad + tip.offsetHeight > window.innerHeight ? e.clientY - tip.offsetHeight - pad : e.clientY + pad;
    tip.style.left = `${x}px`;
    tip.style.top = `${y}px`;
  };
  charts.addEventListener("pointermove", show);
  charts.addEventListener("pointerdown", show); // touch: tap a segment
  charts.addEventListener("pointerleave", () => { tip.hidden = true; });
}

async function main() {
  initTheme();
  initTooltip();
  const algos = $("algos");
  for (const [key, name] of POLICIES) {
    const label = document.createElement("label");
    const input = document.createElement("input");
    input.type = "checkbox";
    input.value = key;
    input.addEventListener("change", scheduleRun);
    label.append(input, name);
    algos.append(label);
  }
  const preset = $("preset");
  for (const [key, pr] of Object.entries(PRESETS)) preset.append(new Option(pr.label, key));
  preset.addEventListener("change", () => applyPreset(preset.value));
  for (const id of ["quantum", "cs", "cores", "aging", "boost", "seed"]) $(id).addEventListener("input", scheduleRun);
  $("add").addEventListener("click", () => {
    const pids = state.procs.map((p) => Number(p.pid) || 0);
    const last = state.procs[state.procs.length - 1];
    state.procs.push({ pid: Math.max(0, ...pids) + 1, arrival: last ? last.arrival : 0, bursts: "3",
                       priority: 0, tickets: 100, nice: 0 });
    renderProcTable();
    scheduleRun();
  });
  $("play").addEventListener("click", () => togglePlay(!state.playing));
  $("scrub").addEventListener("input", (e) => { togglePlay(false); setTime(Number(e.target.value)); });
  let width = window.innerWidth;
  window.addEventListener("resize", () => {
    if (Math.abs(window.innerWidth - width) > 40 && state.result) {
      width = window.innerWidth;
      renderCharts(state.result.results);
    }
  });
  try {
    await loadEngine();
  } catch (e) {
    showError(`Could not load the WebAssembly engine: ${e}`);
    return;
  }
  const asked = new URLSearchParams(location.search).get("preset"); // e.g. ?preset=convoy
  preset.value = asked in PRESETS ? asked : "textbook";
  applyPreset(preset.value);
}

main();
