"""Helpers for running the simulator binary from Python tests."""
import json
import subprocess

ALGOS = ("fcfs", "sjf", "srtf", "rr")


def stdin_text(procs):
    """procs: iterable of (pid, arrival, burst)."""
    procs = list(procs)
    return f"{len(procs)}\n" + "".join(f"{p} {a} {b}\n" for p, a, b in procs)


def run_json(binary, procs, quantum=2, algos="all"):
    """Runs every requested policy and returns {algorithm: result-dict}."""
    r = subprocess.run([binary, "--format=json", f"--quantum={quantum}", f"--algo={algos}"],
                       input=stdin_text(procs), capture_output=True, text=True)
    if r.returncode != 0 or r.stderr:
        raise AssertionError(f"sched failed (exit {r.returncode}) on {procs}: {r.stderr}")
    doc = json.loads(r.stdout)
    return {res["algorithm"]: res for res in doc["results"]}


def gantt_tuples(result):
    return [(s["start"], s["end"], s["pid"]) for s in result["gantt"]]


def start_completion(result):
    return {p["pid"]: (p["start"], p["completion"]) for p in result["processes"]}
