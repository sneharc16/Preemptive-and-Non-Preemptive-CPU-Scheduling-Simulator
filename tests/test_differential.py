"""Differential testing: the C simulator against tools/reference_sim.py.

5,000 seeded random workloads are each run through all four policies. For
every policy the Gantt chart (hence every start and completion time) must be
identical to the tick-by-tick reference, and every schedule must satisfy the
invariants in tests/properties.py. As a cross-policy check, SRTF (optimal for
total turnaround) must never be beaten by any other policy.
"""
import os
import random
from concurrent.futures import ThreadPoolExecutor

import reference_sim
from properties import check_result, total_turnaround
from simrun import ALGOS, gantt_tuples, run_json, start_completion

N_WORKLOADS = int(os.environ.get("SCHED_DIFF_N", "5000"))
SEED = 20260923


def make_workloads(n, seed):
    """A mix of shapes: dense, sparse (idle gaps), all-at-zero, and tie-heavy."""
    rng = random.Random(seed)
    out = []
    for i in range(n):
        shape = i % 4
        size = rng.randint(1, 10)
        pids = rng.sample(range(0, 60), size)
        if shape == 0:  # dense arrivals
            procs = [(p, rng.randint(0, 12), rng.randint(1, 9)) for p in pids]
        elif shape == 1:  # sparse arrivals -> idle gaps
            procs = [(p, rng.randint(0, 60), rng.randint(1, 6)) for p in pids]
        elif shape == 2:  # everyone at t=0
            procs = [(p, 0, rng.randint(1, 9)) for p in pids]
        else:  # few distinct values -> many tie-breaks
            procs = [(p, rng.choice((0, 2, 4)), rng.choice((1, 2, 4))) for p in pids]
        out.append((procs, rng.randint(1, 6)))
    return out


def check_one(binary, procs, quantum):
    got = run_json(binary, procs, quantum)
    for alg in ALGOS:
        ticks = reference_sim.simulate(procs, alg, quantum)
        expected = reference_sim.to_segments(procs, ticks)
        actual = gantt_tuples(got[alg])
        assert actual == expected, (
            f"{alg} q={quantum} differs from reference on {procs}\n"
            f"  C:         {actual}\n  reference: {expected}")
        assert start_completion(got[alg]) == reference_sim.start_completion(procs, ticks)
        check_result(procs, got[alg])
    best = total_turnaround(got["srtf"])
    for alg in ALGOS:
        assert best <= total_turnaround(got[alg]), f"{alg} beat SRTF on {procs}"


def test_differential_against_reference(sched_bin):
    workloads = make_workloads(N_WORKLOADS, SEED)
    workers = min(16, (os.cpu_count() or 2))
    with ThreadPoolExecutor(max_workers=workers) as pool:
        futures = [pool.submit(check_one, sched_bin, procs, q) for procs, q in workloads]
        for f in futures:
            f.result()  # re-raises the first failure with its message
    assert len(futures) == N_WORKLOADS


def test_reference_sim_sanity():
    """The reference itself, on a case small enough to verify by eye."""
    procs = [(1, 0, 4), (2, 2, 2), (3, 1, 1)]
    assert reference_sim.to_segments(procs, reference_sim.rr(procs, 2)) == [
        (0, 2, 1), (2, 3, 3), (3, 5, 2), (5, 7, 1)]
    assert reference_sim.to_segments(procs, reference_sim.srtf(procs)) == [
        (0, 1, 1), (1, 2, 3), (2, 4, 2), (4, 7, 1)]
    assert reference_sim.to_segments([(1, 3, 2)], reference_sim.fcfs([(1, 3, 2)])) == [(3, 5, 1)]
