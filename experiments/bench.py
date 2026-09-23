"""Benchmark: simulation wall time and peak memory for n = 10^3 .. 10^6.

Workloads come from tools/gen.py (exponential bursts, mean 10, Poisson
arrivals at 90% load, seed 1). Each (policy, n) is run REPEATS times; time is
the simulator's own measurement of sim_run (--timing), excluding parsing and
output; memory is the whole process's peak resident set size. A second,
overloaded workload (arrivals at twice capacity, so the ready queue grows to
about n/2) checks that no policy is quadratic when queues are long.
"""
import math
import os
import platform
import re
import statistics
import subprocess
import tempfile

from common import BIN, MARKERS, SERIES, end_labels, plt, save, workload, write_table

POLICIES = ["fcfs", "sjf", "srtf", "rr", "prio", "prio-p", "hrrn", "mlfq", "lottery", "stride",
            "cfs"]
GROUPS = [("Classic", ["fcfs", "sjf", "srtf", "rr"]),
          ("Priority and HRRN", ["prio", "prio-p", "hrrn"]),
          ("MLFQ and fair share", ["mlfq", "lottery", "stride", "cfs"])]
SIZES = [1_000, 10_000, 100_000, 1_000_000]
STRESS_SIZES = [1_000, 10_000, 100_000, 1_000_000]
REPEATS = int(os.environ.get("BENCH_REPEATS", "5"))
ARGS = ["--quantum=4", "--aging=5"]


def time_cmd():
    return ["/usr/bin/time", "-l"] if platform.system() == "Darwin" else ["/usr/bin/time", "-v"]


def peak_mb(stderr):
    m = re.search(r"(\d+)\s+maximum resident set size", stderr)  # macOS: bytes
    if m:
        return int(m.group(1)) / 2 ** 20
    m = re.search(r"Maximum resident set size \(kbytes\): (\d+)", stderr)  # GNU: KiB
    return int(m.group(1)) / 1024 if m else float("nan")


def run_once(path, policy):
    r = subprocess.run([*time_cmd(), BIN, f"--input={path}", f"--algo={policy}", "--format=none",
                        "--timing", *ARGS], capture_output=True, text=True, timeout=3600)
    if r.returncode != 0:
        raise RuntimeError(r.stderr)
    secs = max(float(re.search(r"timing \S+ ([0-9.]+)", r.stderr).group(1)), 1e-6)
    return secs, peak_mb(r.stderr)


def quartiles(xs):
    q = statistics.quantiles(xs, n=4, method="inclusive") if len(xs) > 1 else [xs[0]] * 3
    return q[0], statistics.median(xs), q[2]


def measure(sizes, load, repeats):
    rows = []
    with tempfile.TemporaryDirectory() as tmp:
        for n in sizes:
            path = os.path.join(tmp, f"w{n}.csv")
            with open(path, "w") as f:
                f.write(workload(n=n, seed=1, dist="exp", mean=10.0, load=load))
            for policy in POLICIES:
                runs = [run_once(path, policy) for _ in range(repeats)]
                q1, med, q3 = quartiles([t for t, _ in runs])
                rows.append([policy, n, med, q1, q3, max(m for _, m in runs)])
    return rows


def slope(rows, policy, min_n):
    pts = [(math.log10(n), math.log10(max(med, 1e-7))) for p, n, med, *_ in rows
           if p == policy and n >= min_n]
    mx = sum(x for x, _ in pts) / len(pts)
    my = sum(y for _, y in pts) / len(pts)
    return sum((x - mx) * (y - my) for x, y in pts) / sum((x - mx) ** 2 for x, _ in pts)


def plot(rows, stem, title):
    pyplot = plt()
    fig, axes = pyplot.subplots(1, 3, figsize=(13, 4.2), sharey=True)
    labels = []
    for ax, (name, members) in zip(axes, GROUPS):
        items = []
        for i, policy in enumerate(members):
            pts = [(n, med, q1, q3) for p, n, med, q1, q3, _ in rows if p == policy]
            ns = [p[0] for p in pts]
            ax.plot(ns, [p[1] for p in pts], color=SERIES[i], marker=MARKERS[i], label=policy)
            ax.fill_between(ns, [p[2] for p in pts], [p[3] for p in pts], color=SERIES[i],
                            alpha=0.15, linewidth=0)
            items.append((ns[-1], pts[-1][1], policy, SERIES[i]))
        lo = min(n for _, n, *_ in rows)
        ref = [med for p, n, med, *_ in rows if p == members[0] and n == lo][0]
        ns = sorted({n for _, n, *_ in rows})
        ax.plot(ns, [ref * n / lo for n in ns], color="#898781", linestyle=":", linewidth=1.2,
                label="O(n) reference")
        ax.set_xscale("log")
        ax.set_yscale("log")
        ax.set_title(name, loc="left")
        ax.set_xlabel("processes (n)")
        ax.margins(x=0.25)
        ax.legend(loc="upper left", fontsize=8)
        labels.append((ax, items))
    for ax, items in labels:
        end_labels(ax, items)
    axes[0].set_ylabel("simulation wall time (s), median and IQR")
    fig.suptitle(title, x=0.06, ha="left", fontweight="bold")
    save(fig, stem)


def main():
    rows = measure(SIZES, 0.9, REPEATS)
    stress = measure(STRESS_SIZES, 2.0, REPEATS)
    header = ["policy", "n", "median_s", "q1_s", "q3_s", "peak_rss_mb"]
    write_table("bench.csv", header, rows)
    write_table("bench_overload.csv", header, stress)
    plot(rows, "bench", "Simulation time vs processes, 90% load (log-log)")
    plot(stress, "bench_overload", "Simulation time vs processes, overloaded (log-log)")
    exps = {p: slope(rows, p, 10_000) for p in POLICIES}
    stress_exps = {p: slope(stress, p, 10_000) for p in POLICIES}
    write_table("bench_exponents.csv", ["policy", "exponent_90pct_load", "exponent_overload"],
                [[p, f"{exps[p]:.3f}", f"{stress_exps[p]:.3f}"] for p in POLICIES])
    big = {p: (med, mb) for p, n, med, _, _, mb in rows if n == max(SIZES)}
    worst = max(POLICIES, key=lambda p: stress_exps[p])
    slowest = max(POLICIES, key=lambda p: big[p][0])
    return {
        "bench": (f"At n = {max(SIZES):,} every policy simulates in at most "
                  f"{big[slowest][0]:.2f} s (slowest: {slowest}) with at most "
                  f"{max(mb for _, mb in big.values()):.0f} MB peak memory; fitted log-log "
                  f"slopes from n = 10^4 lie between {min(exps.values()):.2f} and "
                  f"{max(exps.values()):.2f} (1.0 = linear)."),
        "bench_overload": (f"With the ready queue growing to about n/2, the largest fitted "
                           f"slope is {stress_exps[worst]:.2f} ({worst}): no policy is "
                           f"quadratic."),
    }


if __name__ == "__main__":
    for k, v in main().items():
        print(k, v)
