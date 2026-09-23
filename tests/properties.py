"""Invariants every schedule produced by the simulator must satisfy.

check_result(procs, result) raises AssertionError with a description on the
first violated property. `result` is one entry of the JSON "results" array.
"""


def check_result(procs, result):
    alg = result["algorithm"]
    ctx = f"{alg} on {procs}"
    arrival = {p: a for p, a, _ in procs}
    burst = {p: b for p, _, b in procs}
    rows = {r["pid"]: r for r in result["processes"]}
    gantt = [(s["start"], s["end"], s["pid"]) for s in result["gantt"]]

    assert sorted(rows) == sorted(arrival), f"{ctx}: result pids differ from the workload"
    assert gantt, f"{ctx}: empty Gantt chart"

    # Segments are non-empty, ordered, and neither overlap nor leave gaps; the
    # timeline covers exactly [first arrival, makespan].
    first_arrival = min(arrival.values())
    makespan = max(r["completion"] for r in rows.values())
    assert gantt[0][0] == first_arrival, f"{ctx}: timeline starts at {gantt[0][0]}"
    assert gantt[-1][1] == makespan, f"{ctx}: timeline ends at {gantt[-1][1]}"
    assert result["makespan"] == makespan, f"{ctx}: makespan field"
    for (s, e, pid), nxt in zip(gantt, gantt[1:] + [None]):
        assert s < e, f"{ctx}: empty segment {(s, e, pid)}"
        if nxt is not None:
            assert e == nxt[0], f"{ctx}: gap or overlap between {(s, e, pid)} and {nxt}"
            assert pid != nxt[2], f"{ctx}: uncoalesced neighbours {(s, e, pid)} {nxt}"
        assert pid is None or pid in arrival, f"{ctx}: unknown pid {pid}"

    # Each process executes for exactly its burst, only between its arrival
    # and completion, and its first/last segment define start/completion.
    ran = {p: 0 for p in arrival}
    first, last = {}, {}
    for s, e, pid in gantt:
        if pid is None:
            continue
        ran[pid] += e - s
        first.setdefault(pid, s)
        last[pid] = e
        assert s >= arrival[pid], f"{ctx}: P{pid} runs at {s} before arriving at {arrival[pid]}"
    for p in arrival:
        assert ran[p] == burst[p], f"{ctx}: P{p} ran {ran[p]} ticks, burst is {burst[p]}"
        r = rows[p]
        assert (r["start"], r["completion"]) == (first[p], last[p]), f"{ctx}: P{p} start/completion"
        assert r["arrival"] == arrival[p] and r["burst"] == burst[p], f"{ctx}: P{p} echo"

    # Non-preemptive policies run each process in one piece.
    if not result["preemptive"]:
        pieces = [pid for _, _, pid in gantt if pid is not None]
        assert len(pieces) == len(set(pieces)), f"{ctx}: non-preemptive policy split a burst"

    # Work-conserving: the CPU is never idle while some process is ready.
    for s, e, pid in gantt:
        if pid is not None:
            continue
        for p in arrival:
            lo, hi = max(s, arrival[p]), min(e, rows[p]["completion"])
            assert lo >= hi, f"{ctx}: CPU idle during [{s},{e}) while P{p} was ready"

    # Metric identities and bounds.
    n = len(rows)
    for p, r in rows.items():
        assert r["completion"] >= r["arrival"] + r["burst"], f"{ctx}: P{p} finished too early"
        assert r["response"] == r["start"] - r["arrival"] >= 0, f"{ctx}: P{p} response"
        assert r["turnaround"] == r["completion"] - r["arrival"], f"{ctx}: P{p} turnaround"
        assert r["waiting"] == r["turnaround"] - r["burst"], f"{ctx}: P{p} waiting"
        assert r["response"] <= r["waiting"], f"{ctx}: P{p} response > waiting"
    for key in ("response", "waiting", "turnaround"):
        mean = sum(r[key] for r in rows.values()) / n
        assert abs(result["averages"][key] - mean) <= 1e-9 * max(1.0, abs(mean)), f"{ctx}: {key} mean"


def total_turnaround(result):
    return sum(r["turnaround"] for r in result["processes"])
