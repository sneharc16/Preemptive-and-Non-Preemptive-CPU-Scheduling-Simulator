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
const FIELDS = ["pid", "arrival", "bursts", "priority", "tickets", "nice"];
const ADVANCED = new Set(["priority", "tickets", "nice"]);
const DEFAULTS = { priority: 0, tickets: 100, nice: 0 };
const SETTINGS = ["quantum", "aging", "boost", "seed", "cs", "cores"];
const $ = (id) => document.getElementById(id);

const state = { procs: [], result: null, time: 0, playing: false, span: [0, 1], last: 0, speed: 12 };
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

// ---------------------------------------------------------------- workload

function setProcs(procs) {
  state.procs = procs.map(([pid, arrival, bursts, priority = 0, tickets = 100, nice = 0]) =>
    ({ pid, arrival, bursts: String(bursts), priority, tickets, nice }));
  const usesAdvanced = state.procs.some((p) =>
    Number(p.priority) !== 0 || Number(p.tickets) !== 100 || Number(p.nice) !== 0);
  setMoreColumns(usesAdvanced);
  renderProcTable();
}

function setMoreColumns(on) {
  $("procs").classList.toggle("more", on);
  $("more").setAttribute("aria-pressed", String(on));
}

function renderProcTable() {
  const body = $("procs").tBodies[0];
  body.replaceChildren();
  state.procs.forEach((p, i) => {
    const tr = document.createElement("tr");
    for (const f of FIELDS) {
      const td = document.createElement("td");
      if (ADVANCED.has(f)) td.className = "adv";
      const input = document.createElement("input");
      input.value = p[f];
      input.inputMode = f === "bursts" ? "text" : "numeric";
      input.setAttribute("aria-label", `${f} of process ${i + 1}`);
      input.addEventListener("input", () => {
        p[f] = input.value.trim();
        $("preset").value = "custom";
        scheduleRun();
      });
      td.append(input);
      tr.append(td);
    }
    const td = document.createElement("td");
    const rm = document.createElement("button");
    rm.type = "button";
    rm.className = "remove";
    rm.textContent = "×";
    rm.setAttribute("aria-label", `Remove process ${i + 1}`);
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

function randomWorkload() {
  const rnd = (lo, hi) => lo + Math.floor(Math.random() * (hi - lo + 1));
  const n = rnd(5, 7);
  const procs = [];
  let t = 0;
  for (let pid = 1; pid <= n; pid++) {
    procs.push([pid, t, String(rnd(1, 9)), rnd(0, 4), [50, 100, 200][rnd(0, 2)], 0]);
    t += rnd(0, 3);
  }
  return procs;
}

// ---------------------------------------------------------------- controls & state

function selectedAlgos() {
  return [...document.querySelectorAll("#algos input:checked")].map((i) => i.value);
}

function params() {
  const out = {};
  for (const id of SETTINGS) out[id] = $(id).value.trim() || "0";
  return out;
}

function syncSettingsVisibility() {
  const algos = new Set(selectedAlgos());
  document.querySelectorAll(".fields label[data-for]").forEach((label) => {
    label.hidden = !label.dataset.for.split(",").some((a) => algos.has(a));
  });
}

function applySetup({ procs, algos, settings }) {
  document.querySelectorAll("#algos input").forEach((i) => { i.checked = algos.includes(i.value); });
  const p = { quantum: 2, cs: 0, cores: 1, aging: 0, boost: 0, seed: 1, ...settings };
  for (const k of SETTINGS) $(k).value = p[k];
  setProcs(procs);
  syncSettingsVisibility();
}

function applyPreset(key, animate = true) {
  const pr = PRESETS[key];
  $("preset").value = key;
  $("preset-note").textContent = pr.note;
  applySetup({ procs: pr.procs, algos: pr.algos, settings: pr.params });
  run({ animate });
}

// The whole setup lives in the URL hash so a link reproduces it exactly.
function encodeSetup() {
  const procs = state.procs.map((p) => FIELDS.map((f) => p[f]));
  const json = JSON.stringify({ p: procs, a: selectedAlgos(), s: params() });
  const bytes = new TextEncoder().encode(json);
  let bin = "";
  for (const b of bytes) bin += String.fromCharCode(b);
  return btoa(bin).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
}

function decodeSetup(hash) {
  try {
    const bin = atob(hash.replace(/-/g, "+").replace(/_/g, "/"));
    const obj = JSON.parse(new TextDecoder().decode(Uint8Array.from(bin, (c) => c.charCodeAt(0))));
    if (!Array.isArray(obj.p) || !Array.isArray(obj.a)) return null;
    return { procs: obj.p, algos: obj.a, settings: obj.s || {} };
  } catch {
    return null;
  }
}

function scheduleRun() {
  clearTimeout(runTimer);
  runTimer = setTimeout(() => run({ animate: false }), 250);
}

function run({ animate }) {
  if (!engine) return;
  syncSettingsVisibility();
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
    showError("Pick at least one policy to compare.");
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
  history.replaceState(null, "", `${location.pathname}${location.search}#${encodeSetup()}`);
  state.result = res.doc;
  const results = res.doc.results;
  const first = Math.min(...res.doc.workload.map((w) => w.arrival));
  const last = Math.max(...results.map((r) => r.makespan));
  state.span = [first, last];
  $("scrub").min = first;
  $("scrub").max = last;
  renderTiles(results, res.doc);
  renderLegend(res.doc.workload);
  renderCharts(results);
  renderMetrics(results, res.doc, notes);
  if (animate && !matchMedia("(prefers-reduced-motion: reduce)").matches) {
    setTime(first);
    togglePlay(true);
  } else {
    setTime(state.playing ? Math.min(state.time, last) : last);
  }
}

function showError(msg) {
  $("error").hidden = !msg;
  $("error").textContent = msg;
}

function toast(msg) {
  const t = $("toast");
  t.textContent = msg;
  t.hidden = false;
  clearTimeout(toast.timer);
  toast.timer = setTimeout(() => { t.hidden = true; }, 1800);
}

// ---------------------------------------------------------------- highlights

const policyName = (a) => (POLICIES.find(([k]) => k === a) || [a, a])[1];
const fmt2 = (v) => v.toFixed(2);

function tile(k, v, s) {
  const el = document.createElement("div");
  el.className = "tile";
  for (const [cls, text] of [["k", k], ["v", v], ["s", s]]) {
    const d = document.createElement("div");
    d.className = cls;
    d.textContent = text;
    el.append(d);
  }
  return el;
}

function renderTiles(results, doc) {
  const box = $("tiles");
  box.replaceChildren();
  const best = (f, dir) => results.reduce((b, r) => (dir * f(r) < dir * f(b) ? r : b));
  const tiles = [
    ["Lowest mean turnaround", best((r) => r.summary.turnaround.mean, 1), (r) => fmt2(r.summary.turnaround.mean)],
    ["Fastest mean response", best((r) => r.summary.response.mean, 1), (r) => fmt2(r.summary.response.mean)],
    ["Fairest (Jain index)", best((r) => r.summary.jain_fairness_slowdown, -1), (r) => fmt2(r.summary.jain_fairness_slowdown)],
  ];
  for (const [k, r, f] of tiles) box.append(tile(k, f(r), policyName(r.algorithm)));
  if (doc.optimum_mean_turnaround !== undefined) {
    box.append(tile("Exact optimum (non-preemptive)", fmt2(doc.optimum_mean_turnaround), "mean turnaround"));
  } else {
    const fewest = best((r) => r.summary.context_switches, 1);
    box.append(tile("Fewest context switches", String(fewest.summary.context_switches), policyName(fewest.algorithm)));
  }
}

// ---------------------------------------------------------------- timeline

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
  if (workload.length > MAX_COLOURED) return "var(--neutral-run)";
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
    s.textContent = "Segments are labelled with their PID.";
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

const LANE_H = 30;
const BAR_H = 22;
const AXIS_H = 18;

function renderCharts(results) {
  const box = $("charts");
  box.replaceChildren();
  const [t0, t1] = state.span;
  const ticks = Math.max(1, t1 - t0);
  const narrow = box.clientWidth < 520;
  const avail = Math.max(200, box.clientWidth - (narrow ? 0 : 162) - 4);
  const px = Math.max(3, Math.min(56, avail / ticks));
  const width = ticks * px + 2;
  const workload = state.result.workload;
  const x = (t) => 1 + (t - t0) * px;
  const step = niceStep(ticks, (ticks * px) / 64);
  const bestTat = Math.min(...results.map((r) => r.summary.turnaround.mean));

  results.forEach((r, idx) => {
    const row = document.createElement("div");
    row.className = "row";
    const head = document.createElement("div");
    const name = document.createElement("div");
    name.className = "name";
    name.textContent = policyName(r.algorithm) + (r.quantum ? ` · q ${r.quantum}` : "");
    if (results.length > 1 && r.summary.turnaround.mean === bestTat) {
      const pill = document.createElement("span");
      pill.className = "best-mark";
      pill.textContent = "best";
      pill.title = "Lowest mean turnaround";
      name.append(pill);
    }
    const stat = document.createElement("div");
    stat.className = "stat";
    for (const [label, value] of [["turnaround", r.averages.turnaround], ["response", r.averages.response]]) {
      const line = document.createElement("span");
      const num = document.createElement("b");
      num.textContent = fmt2(value);
      line.append(`${label} `, num);
      stat.append(line);
    }
    head.append(name, stat);

    const scroll = document.createElement("div");
    scroll.className = "scroll";
    const showAxis = idx === results.length - 1;
    const height = r.cores * LANE_H + (showAxis ? AXIS_H : 0);
    const svg = svgEl("svg", { width, height, viewBox: `0 0 ${width} ${height}`, role: "img",
      "aria-label": `Timeline for ${policyName(r.algorithm)}` });
    const defs = svgEl("defs");
    const pat = svgEl("pattern", { id: `hatch-${r.algorithm}`, width: 4, height: 4,
      patternUnits: "userSpaceOnUse", patternTransform: "rotate(45)" });
    pat.append(svgEl("rect", { width: 4, height: 4, fill: "var(--track)" }),
               svgEl("line", { x1: 0, y1: 0, x2: 0, y2: 4, stroke: "var(--muted)", "stroke-width": 1.5 }));
    const clip = svgEl("clipPath", { id: `clip-${r.algorithm}` });
    const clipRect = svgEl("rect", { x: 0, y: 0, width: x(state.time), height });
    clip.append(clipRect);
    defs.append(pat, clip);
    svg.append(defs);

    for (let c = 0; c < r.cores; c++) {
      const y = c * LANE_H + (LANE_H - BAR_H) / 2;
      svg.append(svgEl("rect", { x: 1, y, width: ticks * px, height: BAR_H, rx: 6, fill: "var(--track)" }));
    }
    const segs = svgEl("g", { "clip-path": `url(#clip-${r.algorithm})` });
    for (const s of r.gantt) {
      if (s.type === "idle") continue;
      const y = s.core * LANE_H + (LANE_H - BAR_H) / 2;
      const w = Math.max(1, (s.end - s.start) * px - 1.5);
      const fill = s.type === "cs" ? `url(#hatch-${r.algorithm})` : colourOf(s.pid, workload);
      const rect = svgEl("rect", { x: x(s.start) + 0.75, y, width: w, height: BAR_H, rx: 5, fill,
        class: "seg-rect" });
      const len = s.end - s.start;
      rect.dataset.tip = `${s.type === "cs" ? "Switch to " : ""}P${s.pid} · ${s.start}–${s.end} · ${len} tick${len === 1 ? "" : "s"}${r.cores > 1 ? ` · core ${s.core}` : ""}`;
      segs.append(rect);
      if (s.type === "run" && w >= 24) {
        const t = svgEl("text", { x: x(s.start) + 0.75 + w / 2, y: y + BAR_H / 2 + 3.5,
          "text-anchor": "middle", class: "seg-label" });
        t.textContent = `P${s.pid}`;
        segs.append(t);
      }
    }
    svg.append(segs);
    if (r.cores > 1) {
      for (let c = 0; c < r.cores; c++) {
        const label = svgEl("text", { x: 6, y: c * LANE_H + LANE_H / 2 + 3.5, class: "lane-text" });
        label.textContent = `core ${c}`;
        svg.append(label);
      }
    }
    if (showAxis) {
      const axisY = r.cores * LANE_H + 12;
      for (let t = Math.ceil(t0 / step) * step; t <= t1; t += step) {
        const label = svgEl("text", { x: Math.min(Math.max(x(t), 6), width - 6), y: axisY,
          "text-anchor": "middle", class: "axis-text" });
        label.textContent = t;
        svg.append(label);
      }
    }
    const playhead = svgEl("line", { x1: x(state.time), x2: x(state.time), y1: 0, y2: r.cores * LANE_H,
      stroke: "var(--ink)", "stroke-width": 1.5, "stroke-linecap": "round" });
    svg.append(playhead);
    svg._update = (time) => {
      clipRect.setAttribute("width", x(time));
      playhead.setAttribute("x1", x(time));
      playhead.setAttribute("x2", x(time));
      playhead.style.opacity = time >= t1 ? 0 : 1;
    };
    scroll.append(svg);
    row.append(head, scroll);
    box.append(row);
  });
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
  const next = state.time + dt * state.speed;
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
  $("play").setAttribute("aria-label", on ? "Pause" : "Play");
  $("play-icon").setAttribute("d", on ? "M6 4.5h3v11H6zM11 4.5h3v11h-3z" : "M6 4.5v11l9-5.5z");
  if (on) {
    if (state.time >= state.span[1]) setTime(state.span[0]);
    state.last = 0;
    requestAnimationFrame(frame);
  }
}

// ---------------------------------------------------------------- metrics

function renderMetrics(results, doc, notes) {
  // [header, value, which is best, decimals]: averages and ratios always show
  // two decimals, tick counts none, so a column reads consistently.
  const cols = [
    ["Mean turnaround", (r) => r.summary.turnaround.mean, "min", 2],
    ["Mean waiting", (r) => r.summary.waiting.mean, "min", 2],
    ["Mean response", (r) => r.summary.response.mean, "min", 2],
    ["p95 turnaround", (r) => r.summary.turnaround.p95, "min", 0],
    ["Max waiting", (r) => r.summary.waiting.max, "min", 0],
    ["Jain fairness", (r) => r.summary.jain_fairness_slowdown, "max", 2],
    ["Switches", (r) => r.summary.context_switches, "min", 0],
  ];
  if (results.every((r) => "optimality_gap_pct" in r)) {
    cols.push(["Gap vs optimum", (r) => r.optimality_gap_pct, "none", 2, "%"]);
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
    cols.forEach(([, f, , decimals, unit = ""], i) => {
      const td = tr.insertCell();
      const v = f(r);
      td.textContent = v.toFixed(decimals) + unit;
      if (best[i] !== null && results.length > 1 && v === best[i]) {
        td.className = "best";
        td.title = "Best in this column";
      }
    });
  }
  const extra = ["Highlighted values are the best in each column."];
  if (doc.optimum_mean_turnaround !== undefined) {
    extra.push("The gap compares against the exact non-preemptive optimum; preemptive policies can beat it.");
  }
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
    $("theme").setAttribute("aria-label", `Theme: ${mode}`);
    $("theme").title = `Theme: ${mode} (click to change)`;
  };
  $("theme").addEventListener("click", () => {
    mode = modes[(modes.indexOf(mode) + 1) % modes.length];
    try { localStorage.setItem("theme", mode); } catch { /* ignore */ }
    apply();
    toast(`Theme: ${mode}`);
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
  preset.append(new Option("Custom", "custom"));
  preset.addEventListener("change", () => { if (preset.value !== "custom") applyPreset(preset.value); });
  for (const id of SETTINGS) $(id).addEventListener("input", scheduleRun);
  $("add").addEventListener("click", () => {
    const pids = state.procs.map((p) => Number(p.pid) || 0);
    const last = state.procs[state.procs.length - 1];
    state.procs.push({ pid: Math.max(0, ...pids) + 1, arrival: last ? last.arrival : 0, bursts: "3",
                       priority: 0, tickets: 100, nice: 0 });
    $("preset").value = "custom";
    renderProcTable();
    scheduleRun();
  });
  $("random").addEventListener("click", () => {
    $("preset").value = "custom";
    $("preset-note").textContent = "A random workload. Press Random again for another.";
    setProcs(randomWorkload());
    run({ animate: true });
  });
  $("more").addEventListener("click", () => setMoreColumns(!$("procs").classList.contains("more")));
  $("share").addEventListener("click", async () => {
    try {
      await navigator.clipboard.writeText(location.href);
      toast("Link copied");
    } catch {
      toast("Copy the address bar to share this setup");
    }
  });
  $("play").addEventListener("click", () => togglePlay(!state.playing));
  $("scrub").addEventListener("input", (e) => { togglePlay(false); setTime(Number(e.target.value)); });
  document.querySelectorAll(".seg button").forEach((b) => b.addEventListener("click", () => {
    document.querySelectorAll(".seg button").forEach((o) => o.classList.toggle("on", o === b));
    state.speed = Number(b.dataset.speed);
  }));
  let width = window.innerWidth;
  window.addEventListener("resize", () => {
    if (Math.abs(window.innerWidth - width) > 40 && state.result) {
      width = window.innerWidth;
      renderCharts(state.result.results);
      setTime(state.time);
    }
  });
  try {
    await loadEngine();
  } catch (e) {
    showError(`Could not load the simulator engine: ${e}`);
    return;
  }
  const shared = location.hash.length > 1 ? decodeSetup(location.hash.slice(1)) : null;
  if (shared) {
    preset.value = "custom";
    $("preset-note").textContent = "A shared setup.";
    applySetup(shared);
    run({ animate: true });
    return;
  }
  const asked = new URLSearchParams(location.search).get("preset"); // e.g. ?preset=convoy
  applyPreset(asked in PRESETS ? asked : "textbook");
}

main();
