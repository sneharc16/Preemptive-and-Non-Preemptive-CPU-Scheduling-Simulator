"""Differential testing: the C simulator against tools/reference_sim.py.

SCHED_DIFF_N (default 5,000) seeded random workloads, each with randomly
chosen options (cores, context-switch cost, I/O phases, burst prediction,
quantum, aging, MLFQ levels/allotments/boost, lottery seed, CFS parameters),
are run through every online policy. For each policy the per-core Gantt
chart (including switch segments), every start and completion time, and the
switch accounting must equal the tick-by-tick reference, and the schedule
must satisfy the invariants in tests/properties.py. Where the theorem
applies (one core, no switch cost, no I/O, exact bursts), SRTF must also
achieve the smallest total turnaround of all policies.
"""
import os
import random
from concurrent.futures import ProcessPoolExecutor

import reference_sim
from properties import check_result, total_turnaround
from reference_sim import Proc
from simrun import ONLINE, run_procs, start_completion, typed_segments

N_WORKLOADS = int(os.environ.get("SCHED_DIFF_N", "5000"))
SEED = int(os.environ.get("SCHED_DIFF_SEED", "20260923"))


def make_case(rng, i):
    shape = i % 5
    n = rng.randint(1, 8)
    pids = rng.sample(range(0, 60), n)
    io_prob = {0: 0.0, 1: 0.0, 2: 0.0, 3: 0.0, 4: 0.6}[shape] if i % 10 < 5 else 0.3
    procs = []
    for pid in pids:
        if shape == 0:  # dense arrivals
            arrival = rng.randint(0, 12)
        elif shape == 1:  # sparse arrivals -> idle gaps
            arrival = rng.randint(0, 50)
        elif shape == 2:  # everyone at t=0
            arrival = 0
        else:  # few distinct values -> many ties
            arrival = rng.choice((0, 2, 4))
        if rng.random() < io_prob:
            phases = [rng.randint(1, 6) for _ in range(rng.choice((3, 5)))]
        elif shape == 3:
            phases = [rng.choice((1, 2, 4))]
        else:
            phases = [rng.randint(1, 9)]
        procs.append(Proc(pid, arrival, phases, priority=rng.randint(0, 4),
                          tickets=rng.choice((1, 50, 100, 300)), nice=rng.randint(-20, 19)))
    levels = rng.randint(1, 3)
    quanta = sorted(rng.randint(1, 6) for _ in range(levels))
    opts = dict(
        quantum=rng.randint(1, 5),
        aging=rng.choice((0, 0, 1, 3, 5)),
        mlfq_quanta=quanta,
        mlfq_allot=[rng.randint(1, 8) for _ in range(levels)] if rng.random() < 0.5 else None,
        mlfq_boost=rng.choice((0, 0, 7, 15)),
        seed=rng.randint(0, 2 ** 32),
        cfs_latency=rng.randint(4, 30),
        cfs_min_gran=rng.randint(1, 4),
        cores=rng.choice((1, 1, 1, 2, 3)),
        cs_cost=rng.choice((0, 0, 1, 2)),
        predict=rng.choice(("oracle", "oracle", "ewma")),
    )
    if opts["predict"] == "ewma":
        opts["alpha"] = rng.choice((0.0, 0.25, 0.5, 0.8, 1.0))
        opts["tau0"] = rng.choice((1.0, 2.5, 5.0, 10.0))
    return procs, opts


def check_case(binary, procs, opts):
    got, _ = run_procs(binary, procs, **opts)
    for alg in ONLINE:
        ref = reference_sim.simulate(procs, alg, **opts)
        res = got[alg]
        where = f"{alg} with {opts} on {[(p.pid, p.arrival, p.phases, p.priority, p.tickets, p.nice) for p in procs]}"
        for core in range(opts["cores"]):
            expected = reference_sim.segments(ref, core)
            actual = typed_segments(res, core)
            assert actual == expected, (f"{where}\n  core {core}\n  C:         {actual}\n"
                                        f"  reference: {expected}")
        expected_times = {p: (ref.start[p], ref.completion[p]) for p in ref.completion}
        assert start_completion(res) == expected_times, where
        assert res["summary"]["context_switches"] == ref.switches, where
        assert res["summary"]["switch_time"] == ref.cs_time, where
        check_result(procs, res, opts["cs_cost"])
    exact = (opts["cores"] == 1 and opts["cs_cost"] == 0 and opts["predict"] == "oracle"
             and all(len(p.phases) == 1 for p in procs))
    if exact:
        best = total_turnaround(got["srtf"])
        for alg in ONLINE:
            assert best <= total_turnaround(got[alg]), f"{alg} beat SRTF on {procs}"


def check_chunk(binary, chunk):
    for procs, opts in chunk:
        check_case(binary, procs, opts)
    return len(chunk)


def test_differential_against_reference(sched_bin):
    rng = random.Random(SEED)
    cases = [make_case(rng, i) for i in range(N_WORKLOADS)]
    workers = min(16, os.cpu_count() or 2)
    chunks = [cases[i::workers * 4] for i in range(workers * 4)]
    with ProcessPoolExecutor(max_workers=workers) as pool:
        checked = sum(pool.map(check_chunk, [sched_bin] * len(chunks), chunks))
    assert checked == N_WORKLOADS


def test_reference_sim_sanity():
    """The reference itself, on cases small enough to verify by eye."""
    procs = [reference_sim.simple(1, 0, 4), reference_sim.simple(2, 2, 2),
             reference_sim.simple(3, 1, 1)]
    seg = lambda alg, **o: [(s, e, pid) for s, e, _, pid in
                            reference_sim.segments(reference_sim.simulate(procs, alg, **o))]
    assert seg("rr", quantum=2) == [(0, 2, 1), (2, 3, 3), (3, 5, 2), (5, 7, 1)]
    assert seg("srtf") == [(0, 1, 1), (1, 2, 3), (2, 4, 2), (4, 7, 1)]
    one = [reference_sim.simple(1, 3, 2)]
    assert reference_sim.segments(reference_sim.simulate(one, "fcfs")) == [(3, 5, "run", 1)]
    # Bursts 2 then 4, alpha 0.5, tau0 5: predictions 5 then 3.5.
    count, mae, mape = reference_sim.prediction_error([Proc(1, 0, [2, 1, 4])], 0.5, 5.0)
    assert (count, mae, mape) == (2, 1.75, 81.25)
