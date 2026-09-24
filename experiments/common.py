"""Shared helpers for the experiments: running the simulator, plot style,
and writing results. Every number an experiment reports is computed from
simulator output here; nothing is typed in by hand."""
import csv
import json
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RESULTS = os.path.join(ROOT, "docs", "results")
sys.path.insert(0, os.path.join(ROOT, "tools"))

import gen  # noqa: E402  (tools/gen.py)

BIN = os.environ.get("SCHED_BIN", os.path.join(ROOT, "build", "sched"))

# Validated categorical slots (colour-blind-safe order; see the dataviz
# palette): blue, orange, aqua, yellow. Never more than four per chart.
SERIES = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100"]
MARKERS = ["o", "s", "^", "D"]
INK, INK2, MUTED, GRID, AXIS = "#0b0b0b", "#52514e", "#898781", "#e1e0d9", "#c3c2b7"
SURFACE = "#fcfcfb"


def run_csv(csv_text, args):
    """Runs sched on a CSV workload and returns the parsed JSON document."""
    with tempfile.NamedTemporaryFile("w", suffix=".csv", delete=False) as f:
        f.write(csv_text)
        path = f.name
    try:
        r = subprocess.run([BIN, f"--input={path}", "--format=json", *args],
                           capture_output=True, text=True, timeout=3600)
    finally:
        os.unlink(path)
    if r.returncode != 0:
        raise RuntimeError(f"sched {args} failed: {r.stderr}")
    return json.loads(r.stdout)


def results(doc):
    return {res["algorithm"]: res for res in doc["results"]}


def workload(unweighted=False, **kw):
    """Generated workload as CSV. unweighted=True gives every process priority 0,
    100 tickets and nice 0, so weight-aware policies compete on equal terms."""
    rows = gen.generate(**kw)
    if unweighted:
        for r in rows:
            r.update(priority=0, tickets=100, nice=0)
    return gen.to_csv(rows)


def rows_csv(rows, header):
    return header + "\n" + "\n".join(",".join(str(v) for v in r) for r in rows) + "\n"


def write_table(name, header, rows):
    os.makedirs(RESULTS, exist_ok=True)
    with open(os.path.join(RESULTS, name), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(header)
        w.writerows(rows)


def plt():
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as pyplot

    pyplot.rcParams.update({
        "figure.facecolor": SURFACE, "axes.facecolor": SURFACE, "savefig.facecolor": SURFACE,
        "font.family": "sans-serif", "font.size": 10, "text.color": INK,
        "axes.edgecolor": AXIS, "axes.labelcolor": INK2, "axes.titlesize": 11,
        "axes.titleweight": "bold", "axes.titlecolor": INK, "axes.grid": True,
        "grid.color": GRID, "grid.linewidth": 0.8, "axes.axisbelow": True,
        "xtick.color": MUTED, "ytick.color": MUTED, "xtick.labelcolor": INK2,
        "ytick.labelcolor": INK2, "axes.spines.top": False, "axes.spines.right": False,
        "lines.linewidth": 2, "lines.markersize": 6, "legend.frameon": False,
        "svg.hashsalt": "sched", "svg.fonttype": "none",
    })
    return pyplot


def save(fig, stem):
    os.makedirs(RESULTS, exist_ok=True)
    for ext in ("png", "svg"):
        kw = {"dpi": 160} if ext == "png" else {"metadata": {"Date": None}}
        fig.savefig(os.path.join(RESULTS, f"{stem}.{ext}"), bbox_inches="tight", **kw)
    import matplotlib.pyplot as pyplot

    pyplot.close(fig)


def end_labels(ax, items, gap_pt=11):
    """Direct labels at line ends. items: [(x, y, text, color)]. Call after
    the axis scales and limits are final; labels are pushed apart vertically
    so that none overlap."""
    fig = ax.figure
    fig.canvas.draw()
    to_pt = 72.0 / fig.dpi
    placed = sorted(((ax.transData.transform((x, y))[1] * to_pt, x, y, text, color)
                     for x, y, text, color in items))
    ys = [p[0] for p in placed]
    for i in range(1, len(ys)):  # push upwards until apart
        ys[i] = max(ys[i], ys[i - 1] + gap_pt)
    shift = (sum(p[0] for p in placed) - sum(ys)) / len(ys)  # recentre the block
    for (orig, x, y, text, color), target in zip(placed, ys):
        dy = target + shift - orig
        ax.annotate(text, (x, y), xytext=(14, dy), textcoords="offset points", va="center",
                    fontsize=9, color=INK2)
        ax.annotate("■", (x, y), xytext=(5, dy), textcoords="offset points", va="center",
                    fontsize=8, color=color)
