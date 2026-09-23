"""Helpers for running the simulator binary from Python tests."""
import json
import os
import subprocess
import tempfile

ALGOS = ("fcfs", "sjf", "srtf", "rr")
ONLINE = ("fcfs", "sjf", "srtf", "rr", "prio", "prio-p", "hrrn", "mlfq", "lottery", "stride", "cfs")

FLAG_NAMES = {
    "quantum": "--quantum", "aging": "--aging", "mlfq_boost": "--mlfq-boost", "seed": "--seed",
    "cfs_latency": "--cfs-latency", "cfs_min_gran": "--cfs-min-gran", "cores": "--cores",
    "cs_cost": "--cs-cost", "predict": "--predict", "alpha": "--alpha", "tau0": "--tau0",
}


def stdin_text(procs):
    """procs: iterable of (pid, arrival, burst)."""
    procs = list(procs)
    return f"{len(procs)}\n" + "".join(f"{p} {a} {b}\n" for p, a, b in procs)


def csv_text(procs):
    """procs: iterable of reference_sim.Proc."""
    lines = ["pid,arrival,bursts,priority,tickets,nice"]
    for p in procs:
        bursts = ":".join(str(x) for x in p.phases)
        lines.append(f"{p.pid},{p.arrival},{bursts},{p.priority},{p.tickets},{p.nice}")
    return "\n".join(lines) + "\n"


def option_flags(**opts):
    flags = []
    for key, value in opts.items():
        if key in ("mlfq_quanta", "mlfq_allot"):
            if value:
                flag = "--mlfq-quanta" if key == "mlfq_quanta" else "--mlfq-allot"
                flags.append(f"{flag}={','.join(str(v) for v in value)}")
        elif key == "alpha" or key == "tau0":
            flags.append(f"{FLAG_NAMES[key]}={value!r}")
        else:
            flags.append(f"{FLAG_NAMES[key]}={value}")
    return flags


def run_raw(binary, args, stdin="", timeout=120):
    """A hung simulator fails the test (TimeoutExpired) instead of stalling the suite."""
    return subprocess.run([binary, *args], input=stdin, capture_output=True, text=True,
                          timeout=timeout)


def results_by_algo(stdout):
    doc = json.loads(stdout)
    return {res["algorithm"]: res for res in doc["results"]}


def run_json(binary, procs, quantum=2, algos="all", extra=()):
    """Runs (pid, arrival, burst) tuples through the legacy stdin format."""
    r = run_raw(binary, ["--format=json", f"--quantum={quantum}", f"--algo={algos}", *extra],
                stdin_text(procs))
    if r.returncode != 0 or r.stderr:
        raise AssertionError(f"sched failed (exit {r.returncode}) on {procs}: {r.stderr}")
    return results_by_algo(r.stdout)


def run_procs(binary, procs, algos="all", extra=(), **opts):
    """Runs reference_sim.Proc workloads (any columns) through a CSV file.
    Returns (results by algorithm, whole JSON document)."""
    with tempfile.NamedTemporaryFile("w", suffix=".csv", delete=False) as f:
        f.write(csv_text(procs))
        path = f.name
    try:
        args = ["--format=json", f"--input={path}", f"--algo={algos}", *option_flags(**opts),
                *extra]
        r = run_raw(binary, args)
    finally:
        os.unlink(path)
    if r.returncode != 0 or r.stderr:
        raise AssertionError(f"sched failed (exit {r.returncode}) {args}: {r.stderr}")
    doc = json.loads(r.stdout)
    return {res["algorithm"]: res for res in doc["results"]}, doc


def gantt_tuples(result, core=0):
    """(start, end, pid) for one core, pid None for idle (switch segments included)."""
    return [(s["start"], s["end"], s["pid"]) for s in result["gantt"] if s["core"] == core]


def typed_segments(result, core=0):
    return [(s["start"], s["end"], s["type"], s["pid"]) for s in result["gantt"]
            if s["core"] == core]


def start_completion(result):
    return {p["pid"]: (p["start"], p["completion"]) for p in result["processes"]}
