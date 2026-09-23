"""The seven experiments. Each function writes a CSV table and a PNG/SVG
plot to docs/results/ and returns a one-line takeaway built from the data."""
import random
import statistics

from common import (INK, INK2, MARKERS, MUTED, SERIES, end_labels, plt, results, run_csv,
                    save, workload, write_table)

ONLINE = ["fcfs", "sjf", "srtf", "rr", "prio", "prio-p", "hrrn", "mlfq", "lottery", "stride",
          "cfs"]
DISTS = ["exp", "uniform", "bimodal", "pareto"]
# single-hue sequential ramp (blue 100 -> 700), light = near the best
BLUES = ["#cde2fb", "#b7d3f6", "#9ec5f4", "#86b6ef", "#6da7ec", "#5598e7", "#3987e5",
         "#2a78d6", "#256abf", "#1c5cab", "#184f95", "#104281", "#0d366b"]


def line_panel(ax, xs, series, xlabel, ylabel, title, logx=False, logy=False):
    """series: [(label, ys)] (at most four)."""
    items = []
    for i, (label, ys) in enumerate(series):
        ax.plot(xs, ys, color=SERIES[i], marker=MARKERS[i], label=label)
        items.append((xs[-1], ys[-1], label, SERIES[i]))
    if logx:
        ax.set_xscale("log")
    if logy:
        ax.set_yscale("log")
    ax.set_xlabel(xlabel)
    ax.set_ylabel(ylabel)
    ax.set_title(title, loc="left")
    ax.margins(x=0.08)
    return items


def finish(fig, panels, title, stem):
    fig.subplots_adjust(wspace=0.45)
    for ax, items in panels:
        ax.legend(loc="best", fontsize=8)
        end_labels(ax, items)
    fig.suptitle(title, x=0.02, ha="left", fontweight="bold")
    save(fig, stem)


# 1 ------------------------------------------------------------------------

def policy_comparison():
    metrics = [("mean turnaround", lambda s: s["turnaround"]["mean"], False),
               ("p99 turnaround", lambda s: s["turnaround"]["p99"], False),
               ("mean response", lambda s: s["response"]["mean"], False),
               ("Jain fairness (slowdown)", lambda s: s["jain_fairness_slowdown"], True)]
    table, values = [], {}
    for dist in DISTS:
        res = results(run_csv(workload(n=2000, seed=11, dist=dist, mean=10, load=0.9),
                              ["--algo=all", "--quantum=4"]))
        for alg in ONLINE:
            s = res[alg]["summary"]
            vals = [f(s) for _, f, _ in metrics]
            values[(alg, dist)] = vals
            table.append([dist, alg, *[f"{v:.4f}" for v in vals]])
    write_table("exp1_policy_comparison.csv",
                ["distribution", "policy", "mean_turnaround", "p99_turnaround", "mean_response",
                 "jain_fairness_slowdown"], table)

    pyplot = plt()
    fig, axes = pyplot.subplots(1, 4, figsize=(15, 5.2), sharey=True)
    for m, (ax, (name, _, higher_better)) in enumerate(zip(axes, metrics)):
        grid = []
        for alg in ONLINE:
            row = []
            for dist in DISTS:
                col = [values[(a, dist)][m] for a in ONLINE]
                best = max(col) if higher_better else min(col)
                v = values[(alg, dist)][m]
                row.append(best / v if higher_better else (v / best if best > 0 else 1.0))
            grid.append(row)
        # colour = distance from the best policy in that column: log of the
        # ratio, scaled so the worst cell in this panel gets the darkest step
        import math

        worst = max(math.log(max(r, 1.0)) for row in grid for r in row) or 1.0
        idx = [[int(round(math.log(max(r, 1.0)) / worst * (len(BLUES) - 1))) for r in row]
               for row in grid]
        from matplotlib.colors import ListedColormap

        ax.imshow(idx, cmap=ListedColormap(BLUES), vmin=0, vmax=len(BLUES) - 1, aspect="auto")
        for i, alg in enumerate(ONLINE):
            for j, dist in enumerate(DISTS):
                v = values[(alg, dist)][m]
                text = f"{v:.2f}" if m == 3 else f"{v:.0f}" if v >= 100 else f"{v:.1f}"
                ax.text(j, i, text, ha="center", va="center", fontsize=8,
                        color="#ffffff" if idx[i][j] >= 7 else INK)
        ax.set_xticks(range(len(DISTS)), DISTS)
        ax.set_yticks(range(len(ONLINE)), ONLINE)
        ax.set_title(name + (" (higher is better)" if higher_better else ""), loc="left",
                     fontsize=10)
        ax.grid(False)
        ax.tick_params(length=0)
    fig.suptitle("Policies across burst distributions (n = 2000, 90% load); darker = further "
                 "from the best policy in that column (log scale per panel)", x=0.02, ha="left",
                 fontweight="bold")
    save(fig, "exp1_policy_comparison")

    best_mean = {d: min(ONLINE, key=lambda a: values[(a, d)][0]) for d in DISTS}
    best_resp = {d: min(ONLINE, key=lambda a: values[(a, d)][2]) for d in DISTS}
    return (f"Lowest mean turnaround in every distribution: "
            f"{', '.join(sorted(set(best_mean.values())))}; lowest mean response: "
            f"{', '.join(f'{best_resp[d]} ({d})' for d in DISTS)}; on Pareto bursts FCFS's "
            f"mean turnaround is {values[('fcfs', 'pareto')][0] / values[(best_mean['pareto'], 'pareto')][0]:.1f}x "
            f"the best.")


# 2 ------------------------------------------------------------------------

def quantum_sweep():
    quanta = [1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 128]
    # High-variance (bimodal) bursts: a long quantum degrades to FCFS's convoy
    # effect, a short one pays switch overhead.
    wl = workload(n=1000, seed=12, dist="bimodal", mean=10, load=0.9)
    costs = [0, 1, 2]
    rows, tat, resp = [], {c: [] for c in costs}, {c: [] for c in costs}
    for c in costs:
        for q in quanta:
            s = results(run_csv(wl, ["--algo=rr", f"--quantum={q}", f"--cs-cost={c}"]))["rr"]
            m = s["summary"]
            tat[c].append(m["turnaround"]["mean"])
            resp[c].append(m["response"]["mean"])
            rows.append([c, q, f"{m['turnaround']['mean']:.3f}", f"{m['response']['mean']:.3f}",
                         f"{m['switch_fraction']:.4f}"])
    write_table("exp2_rr_quantum.csv",
                ["cs_cost", "quantum", "mean_turnaround", "mean_response", "switch_fraction"],
                rows)
    pyplot = plt()
    fig, (a1, a2) = pyplot.subplots(1, 2, figsize=(13, 4.3))
    p1 = line_panel(a1, quanta, [(f"switch cost {c}", tat[c]) for c in costs],
                    "quantum (ticks, log)", "mean turnaround (ticks)", "Turnaround", logx=True)
    p2 = line_panel(a2, quanta, [(f"switch cost {c}", resp[c]) for c in costs],
                    "quantum (ticks, log)", "mean response (ticks)", "Response", logx=True)
    finish(fig, [(a1, p1), (a2, p2)], "Round Robin quantum sweep (bimodal bursts, mean 10, "
           "90% load)", "exp2_rr_quantum")
    best = {c: quanta[tat[c].index(min(tat[c]))] for c in costs}
    shape = ("a U shape" if best[2] not in (quanta[0], quanta[-1])
             else f"best at the {'smallest' if best[2] == quanta[0] else 'largest'} quantum tested")
    return (f"With switch cost 2, mean turnaround is {tat[2][0]:.0f} at q=1, "
            f"{min(tat[2]):.0f} at q={best[2]} and {tat[2][-1]:.0f} at q={quanta[-1]} "
            f"({shape}); with free switches the best quantum is {best[0]}. Mean response grows "
            f"from {resp[0][0]:.1f} (q=1) to {resp[0][-1]:.1f} (q={quanta[-1]}).")


# 3 ------------------------------------------------------------------------

def convoy():
    ks = [1, 2, 5, 10, 20, 30, 40, 50]
    rows, series = [], {a: [] for a in ("fcfs", "sjf", "srtf")}
    for k in ks:
        procs = ["pid,arrival,burst", "1,0,100"] + [f"{i + 2},1,2" for i in range(k)]
        res = results(run_csv("\n".join(procs) + "\n", ["--algo=fcfs,sjf,srtf"]))
        for a in series:
            series[a].append(res[a]["averages"]["turnaround"])
        rows.append([k, *[f"{series[a][-1]:.3f}" for a in series]])
    write_table("exp3_convoy.csv", ["short_jobs", "fcfs_mean_turnaround",
                                    "sjf_mean_turnaround", "srtf_mean_turnaround"], rows)
    pyplot = plt()
    fig, ax = pyplot.subplots(figsize=(7.5, 4.3))
    p = line_panel(ax, ks, [(a.upper(), series[a]) for a in series],
                   "short jobs (burst 2) arriving at t=1", "mean turnaround (ticks)",
                   "One 100-tick job at t=0, then k short jobs")
    ax.lines[1].set_linestyle("--")  # SJF coincides with FCFS here; keep FCFS visible
    finish(fig, [(ax, p)], "Convoy effect", "exp3_convoy")
    same = series["sjf"][-1] == series["fcfs"][-1]
    return (f"With 50 short jobs queued behind a 100-tick job, mean turnaround is "
            f"{series['fcfs'][-1]:.1f} under FCFS and {series['sjf'][-1]:.1f} under SJF"
            f"{' (no better: it cannot preempt the job already running)' if same else ''} but "
            f"{series['srtf'][-1]:.1f} under SRTF.")


# 4 ------------------------------------------------------------------------

def heavy_tail():
    alphas = [1.1, 1.3, 1.5, 2.0, 2.5, 3.0]
    algs = ["srtf", "rr", "mlfq", "cfs"]
    slow, p99, rows = {a: [] for a in algs}, {a: [] for a in algs}, []
    for alpha in alphas:
        res = results(run_csv(workload(n=2000, seed=14, dist="pareto", mean=10, load=0.9,
                                       alpha=alpha), [f"--algo={','.join(algs)}", "--quantum=4"]))
        for a in algs:
            s = res[a]["summary"]
            slow[a].append(s["slowdown_mean"])
            p99[a].append(s["turnaround"]["p99"])
            rows.append([alpha, a, f"{s['slowdown_mean']:.4f}", s["turnaround"]["p99"],
                         f"{s['turnaround']['mean']:.3f}"])
    write_table("exp4_heavy_tail.csv",
                ["pareto_alpha", "policy", "mean_slowdown", "p99_turnaround", "mean_turnaround"],
                rows)
    pyplot = plt()
    fig, (a1, a2) = pyplot.subplots(1, 2, figsize=(12, 4.3))
    p1 = line_panel(a1, alphas, [(a.upper(), slow[a]) for a in algs],
                    "Pareto shape alpha (smaller = heavier tail)", "mean slowdown (log)",
                    "Mean slowdown", logy=True)
    p2 = line_panel(a2, alphas, [(a.upper(), p99[a]) for a in algs],
                    "Pareto shape alpha (smaller = heavier tail)", "p99 turnaround (ticks, log)",
                    "Tail turnaround", logy=True)
    finish(fig, [(a1, p1), (a2, p2)], "Heavy-tailed bursts (Pareto, mean 10, 90% load)",
           "exp4_heavy_tail")
    i = alphas.index(1.1)
    ranked = sorted(algs, key=lambda a: slow[a][i])
    return (f"At alpha = 1.1 mean slowdown is {slow['srtf'][i]:.2f} for SRTF, "
            f"{slow['mlfq'][i]:.2f} for MLFQ, {slow['rr'][i]:.2f} for RR and "
            f"{slow['cfs'][i]:.2f} for CFS-lite; MLFQ, which needs no burst knowledge, ranks "
            f"#{ranked.index('mlfq') + 1} of 4.")


# 5 ------------------------------------------------------------------------

def ewma_sweep():
    alphas = [0.0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0]
    # every process I/O-bound: ~5 bursts of ~mean/4, so rate for 90% CPU load
    wl = workload(n=600, seed=15, dist="exp", mean=10, rate=0.9 / (5 * 10 / 4), io_fraction=1.0)
    mae, regret, rows = [], {"sjf": [], "srtf": []}, []
    for a in alphas:
        doc = run_csv(wl, ["--algo=sjf,srtf", "--predict=ewma", f"--alpha={a}", "--tau0=10"])
        res = results(doc)
        mae.append(doc["prediction"]["mae"])
        for alg in regret:
            regret[alg].append(res[alg]["prediction_regret_pct"])
        rows.append([a, f"{doc['prediction']['mae']:.4f}", f"{doc['prediction']['mape_pct']:.3f}",
                     f"{regret['sjf'][-1]:.3f}", f"{regret['srtf'][-1]:.3f}"])
    write_table("exp5_ewma.csv", ["alpha", "mae", "mape_pct", "sjf_regret_pct",
                                  "srtf_regret_pct"], rows)
    pyplot = plt()
    fig, (a1, a2) = pyplot.subplots(1, 2, figsize=(12, 4.3))
    p1 = line_panel(a1, alphas, [("EWMA MAE", mae)], "alpha (weight of the latest burst)",
                    "mean absolute error (ticks)", "Prediction error")
    p2 = line_panel(a2, alphas, [("SJF", regret["sjf"]), ("SRTF", regret["srtf"])],
                    "alpha (weight of the latest burst)", "mean turnaround vs oracle (%)",
                    "Scheduling regret")
    a2.axhline(0, color=MUTED, linewidth=1)
    finish(fig, [(a1, p1), (a2, p2)], "EWMA burst prediction (I/O-bound processes, "
           "correlated bursts, tau0 = 10)", "exp5_ewma")
    best = alphas[mae.index(min(mae))]
    return (f"MAE falls from {mae[0]:.2f} (alpha 0: never learns) to {min(mae):.2f} at alpha "
            f"{best}; SRTF's regret against exact bursts goes from {regret['srtf'][0]:.1f}% to "
            f"{regret['srtf'][alphas.index(best)]:.1f}% and SJF's from {regret['sjf'][0]:.1f}% "
            f"to {regret['sjf'][alphas.index(best)]:.1f}%.")


# 6 ------------------------------------------------------------------------

def starvation():
    periods = [1, 2, 5, 10, 20, 50, 100, 200, 500]
    wl = workload(n=2000, seed=16, dist="exp", mean=10, load=0.97)
    algs = ["prio", "prio-p"]
    off = {}
    curve, rows = {a: [] for a in algs}, []
    res = results(run_csv(wl, ["--algo=prio,prio-p"]))
    for a in algs:
        off[a] = res[a]["summary"]["waiting"]["max"]
        rows.append([a, "off", off[a], f"{res[a]['summary']['waiting']['mean']:.3f}"])
    for k in periods:
        res = results(run_csv(wl, ["--algo=prio,prio-p", f"--aging={k}"]))
        for a in algs:
            curve[a].append(res[a]["summary"]["waiting"]["max"])
            rows.append([a, k, curve[a][-1], f"{res[a]['summary']['waiting']['mean']:.3f}"])
    write_table("exp6_starvation.csv", ["policy", "aging_period", "max_waiting", "mean_waiting"],
                rows)
    pyplot = plt()
    fig, ax = pyplot.subplots(figsize=(8, 4.5))
    p = line_panel(ax, periods, [("PRIO", curve["prio"]), ("PRIO-P", curve["prio-p"])],
                   "aging period K (ticks per priority level, log)", "max waiting (ticks, log)",
                   "Worst-case wait vs aging period", logx=True, logy=True)
    for i, a in enumerate(algs):
        ax.axhline(off[a], color=SERIES[i], linestyle="--", linewidth=1.2)
    if off["prio"] == off["prio-p"]:
        notes = [(off["prio"], f"without aging (both): {off['prio']}")]
    else:
        notes = [(off[a], f"{a.upper()} without aging: {off[a]}") for a in algs]
    for k, (y, text) in enumerate(notes):
        ax.annotate(text, (periods[0], y), xytext=(0, -12 - 12 * k), textcoords="offset points",
                    fontsize=8, color=INK2)
    finish(fig, [(ax, p)], "Starvation: priorities 0-9, 97% load (dashed = no aging)",
           "exp6_starvation")
    return (f"Without aging the longest wait is {off['prio']} ticks (prio) and {off['prio-p']} "
            f"(prio-p); aging every {periods[2]} ticks cuts it to {curve['prio'][2]} and "
            f"{curve['prio-p'][2]}.")


# 7 ------------------------------------------------------------------------

def optimality_gap():
    rng = random.Random(17)
    algs = ["fcfs", "sjf", "hrrn"]
    gaps, rows = {a: [] for a in algs}, []
    for i in range(1000):
        n = rng.randint(4, 10)
        lines = ["pid,arrival,burst"] + [f"{p},{rng.randint(0, 30)},{rng.randint(1, 15)}"
                                         for p in range(1, n + 1)]
        res = results(run_csv("\n".join(lines) + "\n", ["--algo=fcfs,sjf,hrrn", "--gap"]))
        for a in algs:
            gaps[a].append(res[a]["optimality_gap_pct"])
        rows.append([i, n, *[f"{gaps[a][-1]:.4f}" for a in algs]])
    write_table("exp7_optimality_gap.csv", ["instance", "n", "fcfs_gap_pct", "sjf_gap_pct",
                                            "hrrn_gap_pct"], rows)
    pyplot = plt()
    fig, ax = pyplot.subplots(figsize=(8, 4.5))
    items = []
    for i, a in enumerate(algs):
        xs = sorted(gaps[a])
        ys = [(k + 1) / len(xs) for k in range(len(xs))]
        ax.step(xs, ys, where="post", color=SERIES[i], label=a.upper(), linewidth=2)
        mid = xs[len(xs) // 2]
        items.append((mid, 0.5, f"{a.upper()} median {mid:.1f}%", SERIES[i]))
    ax.set_xscale("symlog", linthresh=1)
    ax.set_xlabel("gap above the exact non-preemptive optimum (%, symlog)")
    ax.set_ylabel("fraction of instances")
    ax.set_title("1,000 random instances, 4-10 processes", loc="left")
    finish(fig, [(ax, items)], "Optimality gap of greedy non-preemptive policies vs opt-np",
           "exp7_optimality_gap")
    exact = {a: sum(g <= 1e-9 for g in gaps[a]) / 10 for a in algs}
    return (f"SJF is exactly optimal on {exact['sjf']:.1f}% of instances (median gap "
            f"{statistics.median(gaps['sjf']):.2f}%, worst {max(gaps['sjf']):.1f}%); HRRN on "
            f"{exact['hrrn']:.1f}% (worst {max(gaps['hrrn']):.1f}%); FCFS on "
            f"{exact['fcfs']:.1f}% (worst {max(gaps['fcfs']):.1f}%).")


ALL = [("exp1_policy_comparison", "Policy comparison across burst distributions",
        policy_comparison),
       ("exp2_rr_quantum", "Round Robin quantum sweep, with and without switch cost",
        quantum_sweep),
       ("exp3_convoy", "Convoy effect", convoy),
       ("exp4_heavy_tail", "Heavy-tailed workloads", heavy_tail),
       ("exp5_ewma", "EWMA alpha sweep: prediction error vs scheduling regret", ewma_sweep),
       ("exp6_starvation", "Starvation and aging", starvation),
       ("exp7_optimality_gap", "Optimality gap of greedy non-preemptive policies",
        optimality_gap)]

__all__ = ["ALL", "INK"]
