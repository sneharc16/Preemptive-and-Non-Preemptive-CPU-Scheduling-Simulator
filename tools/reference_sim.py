#!/usr/bin/env python3
"""Deliberately naive tick-by-tick reference simulator.

Written independently of the C engine and shares no code or structure with
it: there is no event queue, no slice clamping and no policy interface. Time
advances one tick at a time, and every tick the CPU either runs exactly one
process for one tick or idles. It is slow on purpose; its only job is to be
obviously correct so that tests/test_differential.py can compare the C
implementation against it.

A workload is a list of (pid, arrival, burst) tuples with unique pids.
Every function returns a list `ticks` where ticks[i] is the pid that ran
during [t0 + i, t0 + i + 1), or None for an idle tick, with t0 = the first
arrival time.

Tie-break rules (the specification being tested):
  FCFS  earliest (arrival, pid)
  SJF   smallest (burst, arrival, pid), non-preemptive
  SRTF  smallest (remaining, arrival, pid), re-evaluated every tick
  RR    FIFO queue; processes arriving during or at the end of a slice are
        queued before the process whose slice just ended
"""


def _first_arrival(procs):
    return min(a for _, a, _ in procs)


def run_non_preemptive(procs, key):
    """FCFS / SJF: when the CPU is free, pick min(key) among arrived jobs and run it to completion."""
    arrival = {p: a for p, a, _ in procs}
    left = {p: b for p, _, b in procs}
    t = _first_arrival(procs)
    ticks = []
    current = None
    while left:
        if current is None:
            ready = [p for p in procs if p[0] in left and arrival[p[0]] <= t]
            if ready:
                current = min(ready, key=key)[0]
        if current is None:
            ticks.append(None)
        else:
            ticks.append(current)
            left[current] -= 1
            if left[current] == 0:
                del left[current]
                current = None
        t += 1
    return ticks


def fcfs(procs):
    return run_non_preemptive(procs, key=lambda p: (p[1], p[0]))


def sjf(procs):
    return run_non_preemptive(procs, key=lambda p: (p[2], p[1], p[0]))


def srtf(procs):
    arrival = {p: a for p, a, _ in procs}
    left = {p: b for p, _, b in procs}
    t = _first_arrival(procs)
    ticks = []
    while left:
        ready = [p for p in left if arrival[p] <= t]
        if not ready:
            ticks.append(None)
        else:
            p = min(ready, key=lambda x: (left[x], arrival[x], x))
            ticks.append(p)
            left[p] -= 1
            if left[p] == 0:
                del left[p]
        t += 1
    return ticks


def rr(procs, quantum):
    left = {p: b for p, _, b in procs}
    t = _first_arrival(procs)
    queue = []
    current, used = None, 0
    ticks = []
    while left:
        # 1. everything arriving at this instant joins the queue (pid order for ties)
        queue.extend(sorted(p for p, a, _ in procs if a == t))
        # 2. then a process whose quantum just expired goes to the back
        if current is not None and used == quantum:
            queue.append(current)
            current = None
        # 3. a free CPU takes the head of the queue
        if current is None and queue:
            current, used = queue.pop(0), 0
        # 4. run one tick
        if current is None:
            ticks.append(None)
        else:
            ticks.append(current)
            left[current] -= 1
            used += 1
            if left[current] == 0:
                del left[current]
                current = None
        t += 1
    return ticks


def simulate(procs, algorithm, quantum=2):
    if algorithm == "fcfs":
        return fcfs(procs)
    if algorithm == "sjf":
        return sjf(procs)
    if algorithm == "srtf":
        return srtf(procs)
    if algorithm == "rr":
        return rr(procs, quantum)
    raise ValueError(algorithm)


def to_segments(procs, ticks):
    """Collapses per-tick pids into [(start, end, pid_or_None), ...]."""
    t0 = _first_arrival(procs)
    segments = []
    for i, pid in enumerate(ticks):
        if segments and segments[-1][2] == pid:
            segments[-1] = (segments[-1][0], t0 + i + 1, pid)
        else:
            segments.append((t0 + i, t0 + i + 1, pid))
    return segments


def start_completion(procs, ticks):
    """{pid: (first tick run, end of last tick run)}."""
    t0 = _first_arrival(procs)
    out = {}
    for i, pid in enumerate(ticks):
        if pid is None:
            continue
        start = out[pid][0] if pid in out else t0 + i
        out[pid] = (start, t0 + i + 1)
    return out


if __name__ == "__main__":
    import sys

    rows = [tuple(int(x) for x in line.split()) for line in sys.stdin if line.strip()]
    q = int(sys.argv[1]) if len(sys.argv) > 1 else 2
    for alg in ("fcfs", "sjf", "srtf", "rr"):
        print(alg, to_segments(rows, simulate(rows, alg, q)))
