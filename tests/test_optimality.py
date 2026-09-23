"""Known optimality theorems, checked against exhaustive search.

1. With every process arriving at t=0, SJF (shortest processing time first)
   minimises total turnaround over all n! execution orders.
2. SRTF (shortest remaining processing time) minimises total turnaround over
   ALL preemptive single-CPU schedules, including ones that idle on purpose.
   The exhaustive search decides, tick by tick, which process (or nothing)
   runs; with integer arrivals and bursts, integer preemption points suffice.
"""
import itertools
import random
from functools import lru_cache

from simrun import run_json
from properties import total_turnaround


def brute_force_min_total_turnaround_all_at_zero(bursts):
    best = None
    for order in itertools.permutations(bursts):
        t = total = 0
        for b in order:
            t += b
            total += t
        best = total if best is None else min(best, total)
    return best


def exact_preemptive_min_total_turnaround(procs):
    arrivals = tuple(a for _, a, _ in procs)
    horizon = max(arrivals) + sum(b for _, _, b in procs)

    @lru_cache(maxsize=None)
    def best(t, remaining):
        if not any(remaining):
            return 0
        if t >= horizon:  # idling this long can never be optimal
            return float("inf")
        options = [best(t + 1, remaining)]  # idle this tick
        for i, r in enumerate(remaining):
            if r > 0 and arrivals[i] <= t:
                nxt = remaining[:i] + (r - 1,) + remaining[i + 1:]
                finished = (t + 1 - arrivals[i]) if r == 1 else 0
                options.append(finished + best(t + 1, nxt))
        return min(options)

    return best(0, tuple(b for _, _, b in procs))


def test_sjf_optimal_when_all_arrive_at_zero(sched_bin):
    rng = random.Random(1)
    for _ in range(300):
        n = rng.randint(1, 7)
        procs = [(pid, 0, rng.randint(1, 12)) for pid in rng.sample(range(1, 100), n)]
        got = run_json(sched_bin, procs, algos="sjf,srtf")
        optimum = brute_force_min_total_turnaround_all_at_zero([b for _, _, b in procs])
        assert total_turnaround(got["sjf"]) == optimum, procs
        assert total_turnaround(got["srtf"]) == optimum, procs


def test_srtf_matches_exact_preemptive_optimum(sched_bin):
    rng = random.Random(2)
    for _ in range(400):
        n = rng.randint(1, 4)
        procs = [(pid, rng.randint(0, 5), rng.randint(1, 4)) for pid in range(1, n + 1)]
        got = run_json(sched_bin, procs, algos="srtf")
        assert total_turnaround(got["srtf"]) == exact_preemptive_min_total_turnaround(procs), procs


def test_exhaustive_search_sanity():
    # P1 [0,3) then P2 [3,4): turnarounds 3 + 2 (preempting at t=2 ties at 1 + 4).
    assert exact_preemptive_min_total_turnaround([(1, 0, 3), (2, 2, 1)]) == 5
    # Preempt P1 at t=1 for P2: P2 turnaround 1, P1 finishes at 5 (non-preemptive would be 8).
    assert exact_preemptive_min_total_turnaround([(1, 0, 4), (2, 1, 1)]) == 1 + 5
    assert brute_force_min_total_turnaround_all_at_zero([3, 1, 2]) == 1 + 3 + 6
