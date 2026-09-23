"""Invariants every schedule produced by the simulator must satisfy.

check_result(procs, result, cs_cost=0) raises AssertionError describing the
first violated property. `procs` is a list of reference_sim.Proc and
`result` one entry of the JSON "results" array.
"""
from collections import defaultdict

OFFLINE = {"opt-np"}  # may idle on purpose


def _segments_by_core(result):
    by_core = defaultdict(list)
    for s in result["gantt"]:
        by_core[s["core"]].append((s["start"], s["end"], s["type"], s["pid"]))
    return [by_core[c] for c in range(result["cores"])]


def check_result(procs, result, cs_cost=0):
    alg = result["algorithm"]
    ctx = f"{alg} on {[(p.pid, p.arrival, p.phases) for p in procs]}"
    info = {p.pid: p for p in procs}
    rows = {r["pid"]: r for r in result["processes"]}
    cores = _segments_by_core(result)
    assert sorted(rows) == sorted(info), f"{ctx}: result pids differ from the workload"

    first_arrival = min(p.arrival for p in procs)
    makespan = max(r["completion"] for r in rows.values())
    assert result["makespan"] == makespan, f"{ctx}: makespan field"

    # Each core's segments are non-empty, contiguous, merged, and cover
    # exactly [first arrival, makespan]. Switch segments last cs_cost ticks.
    runs = defaultdict(list)  # pid -> [(start, end)]
    busy = []  # (start, end, pid, kind) on any core
    for c, segs in enumerate(cores):
        assert segs, f"{ctx}: core {c} has no segments"
        assert segs[0][0] == first_arrival and segs[-1][1] == makespan, f"{ctx}: core {c} span"
        for (s, e, kind, pid), nxt in zip(segs, segs[1:] + [None]):
            assert s < e, f"{ctx}: empty segment on core {c}"
            assert kind in ("run", "idle", "cs"), f"{ctx}: segment type {kind}"
            assert (pid is None) == (kind == "idle"), f"{ctx}: pid/type mismatch"
            if nxt is not None:
                assert e == nxt[0], f"{ctx}: gap or overlap on core {c} at {e}"
                assert (kind, pid) != (nxt[2], nxt[3]), f"{ctx}: unmerged segments on core {c}"
            if kind == "cs":
                assert cs_cost > 0 and e - s == cs_cost, f"{ctx}: switch segment {s}-{e}"
            if kind == "run":
                runs[pid].append((s, e))
            if kind != "idle":
                busy.append((s, e, pid, kind))

    # A process is never on two cores at once.
    by_pid = defaultdict(list)
    for s, e, pid, _ in busy:
        by_pid[pid].append((s, e))
    for pid, spans in by_pid.items():
        spans.sort()
        for (s1, e1), (s2, _) in zip(spans, spans[1:]):
            assert e1 <= s2, f"{ctx}: P{pid} on two cores during [{s2},{e1})"

    # CPU bursts: runs add up to each burst, never before arrival, and each
    # I/O phase separates consecutive bursts. Record I/O intervals.
    io_spans = defaultdict(list)
    for pid, p in info.items():
        spans = sorted(runs[pid])
        assert spans, f"{ctx}: P{pid} never ran"
        assert spans[0][0] >= p.arrival, f"{ctx}: P{pid} ran before arriving"
        r = rows[pid]
        assert r["start"] == spans[0][0] and r["completion"] == spans[-1][1], f"{ctx}: P{pid} times"
        cpu = p.phases[0::2]
        io = p.phases[1::2]
        k, need, ready_at = 0, cpu[0], p.arrival
        for s, e in spans:
            assert s >= ready_at, f"{ctx}: P{pid} ran at {s} during I/O (ready at {ready_at})"
            length = e - s
            while length > 0:
                assert k < len(cpu), f"{ctx}: P{pid} ran more than its bursts"
                used = min(length, need)
                length -= used
                need -= used
                s += used
                if need == 0:
                    if k < len(io):
                        io_spans[pid].append((s, s + io[k]))
                        ready_at = s + io[k]
                        assert length == 0, f"{ctx}: P{pid} kept running into its I/O"
                    k += 1
                    need = cpu[k] if k < len(cpu) else 0
        assert k == len(cpu), f"{ctx}: P{pid} ran {k} of {len(cpu)} bursts"

    # Work conservation: no core is idle while some process is ready (arrived,
    # not finished, not doing I/O, not running or switching in anywhere).
    if alg not in OFFLINE:
        idle = [(s, e) for segs in cores for s, e, kind, _ in segs if kind == "idle"]
        for s, e in idle:
            for tick in range(s, e):
                for pid, p in info.items():
                    if not (p.arrival <= tick < rows[pid]["completion"]):
                        continue
                    if any(a <= tick < b for a, b in io_spans[pid]):
                        continue
                    if any(a <= tick < b for a, b in by_pid[pid]):
                        continue
                    raise AssertionError(f"{ctx}: a core idled at t={tick} while P{pid} was ready")

    # Metric identities and bounds.
    n = len(rows)
    for pid, r in rows.items():
        p = info[pid]
        assert (r["burst"], r["io"]) == (p.cpu, p.io), f"{ctx}: P{pid} echo"
        assert r["completion"] >= p.arrival + p.cpu + p.io, f"{ctx}: P{pid} finished too early"
        assert r["response"] == r["start"] - p.arrival >= 0, f"{ctx}: P{pid} response"
        assert r["turnaround"] == r["completion"] - p.arrival, f"{ctx}: P{pid} turnaround"
        assert r["waiting"] == r["turnaround"] - p.cpu - p.io, f"{ctx}: P{pid} waiting"
        assert 0 <= r["response"] <= r["waiting"], f"{ctx}: P{pid} response > waiting"
        assert abs(r["slowdown"] - r["turnaround"] / p.cpu) < 1e-12, f"{ctx}: P{pid} slowdown"
    for key in ("response", "waiting", "turnaround"):
        mean = sum(r[key] for r in rows.values()) / n
        assert abs(result["averages"][key] - mean) <= 1e-9 * max(1.0, abs(mean)), f"{ctx}: {key}"

    # Summary statistics, recomputed independently.
    s = result["summary"]
    span = makespan - first_arrival
    assert s["span"] == span
    for key in ("turnaround", "waiting", "response"):
        vals = sorted(r[key] for r in rows.values())
        d = s[key]
        mid = n // 2
        median = vals[mid] if n % 2 else (vals[mid - 1] + vals[mid]) / 2
        assert d["median"] == median and d["max"] == vals[-1], f"{ctx}: {key} median/max"
        for pct in (95, 99):
            rank = -(-pct * n // 100)  # ceil
            assert d[f"p{pct}"] == vals[rank - 1], f"{ctx}: {key} p{pct}"
    slow = [r["turnaround"] / info[r["pid"]].cpu for r in rows.values()]
    jain = sum(slow) ** 2 / (n * sum(x * x for x in slow))
    assert abs(s["jain_fairness_slowdown"] - jain) < 1e-9 and 0 < jain <= 1 + 1e-12
    capacity = result["cores"] * span
    assert abs(s["cpu_utilization"] - sum(p.cpu for p in procs) / capacity) < 1e-12
    cs_ticks = sum(e - st for st, e, _, kind in busy if kind == "cs")
    assert s["switch_time"] == cs_ticks, f"{ctx}: switch time"
    if cs_cost > 0:
        assert s["context_switches"] == cs_ticks // cs_cost, f"{ctx}: switch count"
    assert abs(s["switch_fraction"] - cs_ticks / capacity) < 1e-12


def total_turnaround(result):
    return sum(r["turnaround"] for r in result["processes"])
