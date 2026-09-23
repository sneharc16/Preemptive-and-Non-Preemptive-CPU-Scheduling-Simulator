"""Hand-computed cases and behavioural tests for the Session 2 policies and
realism options. Expected Gantt charts were worked out on paper from the
rules documented in each src/policies/*.c file; every one is also checked
against the reference simulator and the schedule invariants.
"""
import itertools
import json
import random

import pytest

import reference_sim
from properties import check_result, total_turnaround
from reference_sim import Proc, simple
from simrun import run_json, run_procs, run_raw, stdin_text, typed_segments


def gantt(result, core=0):
    return [(s, e, pid) for s, e, _, pid in typed_segments(result, core)]


def check_against_reference(procs, result, **opts):
    ref = reference_sim.simulate(procs, result["algorithm"], **opts)
    for core in range(opts.get("cores", 1)):
        assert typed_segments(result, core) == reference_sim.segments(ref, core)
    check_result(procs, result, opts.get("cs_cost", 0))


HAND = {
    # prio: P1 runs to completion; then P2 (priority 1) before P3 (2).
    "prio": dict(
        procs=[Proc(1, 0, [4], priority=3), Proc(2, 1, [2], priority=1),
               Proc(3, 2, [1], priority=2)],
        opts={}, expected=[(0, 4, 1), (4, 6, 2), (6, 7, 3)]),
    # prio-p: P2 (priority 1) preempts P1 at t=1; P3 (2) waits for P2 but
    # runs before P1 (3).
    "prio-p": dict(
        procs=[Proc(1, 0, [4], priority=3), Proc(2, 1, [2], priority=1),
               Proc(3, 2, [1], priority=2)],
        opts={}, expected=[(0, 1, 1), (1, 3, 2), (3, 4, 3), (4, 7, 1)]),
    # hrrn: at t=4, P2 has ratio 1 + 3/8 and P3 1 + 0/2, so the long P2 goes
    # first (SJF would pick P3).
    "hrrn": dict(
        procs=[simple(1, 0, 4), simple(2, 1, 8), simple(3, 4, 2)],
        opts={}, expected=[(0, 4, 1), (4, 12, 2), (12, 14, 3)]),
    # mlfq, quanta 2,4 (allotments equal): P1 uses its top-level allotment by
    # t=2 and drops; P2 arrives at t=3 and preempts it; P1 resumes at the
    # front of level 1 with the rest of its slice.
    "mlfq": dict(
        procs=[simple(1, 0, 7), simple(2, 3, 3)],
        opts=dict(mlfq_quanta=(2, 4)),
        expected=[(0, 3, 1), (3, 5, 2), (5, 8, 1), (8, 9, 2), (9, 10, 1)]),
    # stride, quantum 1, tickets 300:100 (strides 3495 and 10485): passes
    # tie at 10485 at t=3 and the lower pid wins.
    "stride": dict(
        procs=[Proc(1, 0, [4], tickets=300), Proc(2, 0, [2], tickets=100)],
        opts=dict(quantum=1),
        expected=[(0, 1, 1), (1, 2, 2), (2, 5, 1), (5, 6, 2)]),
    # cfs, latency 6, min_gran 1: weights 1024 (nice 0) and 335 (nice 5),
    # so slices are floor(6*1024/1359)=4 and floor(6*335/1359)=1. vruntime
    # (units of 2^-16 nice-0 tick): P1 +65536/tick, P2 +200324/tick.
    # t=4: P1 262144, P2 0 -> P2; t=5: P2 200324 < 262144 -> P2 again;
    # t=6: P2 400648 -> P1 (only 2 ticks left); t=8: P2 alone.
    "cfs": dict(
        procs=[Proc(1, 0, [6], nice=0), Proc(2, 0, [3], nice=5)],
        opts=dict(cfs_latency=6, cfs_min_gran=1),
        expected=[(0, 4, 1), (4, 6, 2), (6, 8, 1), (8, 9, 2)]),
}


@pytest.mark.parametrize("alg", sorted(HAND))
def test_hand_computed_policy(sched_bin, alg):
    case = HAND[alg]
    got, _ = run_procs(sched_bin, case["procs"], algos=alg, **case["opts"])
    assert gantt(got[alg]) == case["expected"]
    check_against_reference(case["procs"], got[alg], **case["opts"])


def test_cfs_vruntime_steps():
    """The vruntime increments the CFS hand case relies on."""
    assert (4 << 26) // 1024 == 262144
    assert (1 << 26) // 335 == 200324


def test_mlfq_allotment_defeats_gaming(sched_bin):
    """A job that gives up the CPU just before its slice expires is still
    demoted once it has used its allotment (OSTEP rule 4). P2 does 1-tick
    bursts separated by 1-tick I/O. After 2 ticks of CPU at the top level it
    drops to level 1, so when it wakes at t=6 it no longer preempts P1."""
    procs = [simple(1, 0, 12), Proc(2, 0, [1, 1, 1, 1, 1, 1, 1])]
    opts = dict(mlfq_quanta=(2, 4), mlfq_allot=(2, 4))
    got, _ = run_procs(sched_bin, procs, algos="mlfq", **opts)
    assert gantt(got["mlfq"]) == [(0, 2, 1), (2, 3, 2), (3, 4, 1), (4, 5, 2), (5, 8, 1),
                                  (8, 9, 2), (9, 13, 1), (13, 14, 2), (14, 16, 1)]
    check_against_reference(procs, got["mlfq"], **opts)
    # With a huge top-level allotment the same job keeps preempting P1.
    gamed, _ = run_procs(sched_bin, procs, algos="mlfq",
                         mlfq_quanta=(2, 4), mlfq_allot=(1000, 4))
    p2_done = {p["pid"]: p["completion"] for p in gamed["mlfq"]["processes"]}[2]
    fair_done = {p["pid"]: p["completion"] for p in got["mlfq"]["processes"]}[2]
    assert p2_done < fair_done


def test_mlfq_boost(sched_bin):
    """Boost every 10 ticks: a demoted CPU hog returns to the top queue and
    shares it with newer jobs instead of waiting behind them."""
    procs = [simple(1, 0, 20)] + [simple(10 + k, 4 + 2 * k, 2) for k in range(8)]
    for boost in (0, 10):
        got, _ = run_procs(sched_bin, procs, algos="mlfq", mlfq_quanta=(2, 4, 8),
                           mlfq_boost=boost)
        check_against_reference(procs, got["mlfq"], mlfq_quanta=(2, 4, 8), mlfq_boost=boost)
    no_boost, _ = run_procs(sched_bin, procs, algos="mlfq", mlfq_boost=0)
    boosted, _ = run_procs(sched_bin, procs, algos="mlfq", mlfq_boost=10)
    hog = lambda res: next(p for p in res["mlfq"]["processes"] if p["pid"] == 1)
    assert hog(boosted)["waiting"] <= hog(no_boost)["waiting"]


def test_priority_starvation_and_aging(sched_bin):
    """P0 (priority 10) arrives at t=0 with a stream of priority-1 jobs, one
    every 2 ticks for 100 ticks. Without aging it runs only after all of them
    (t=100). With --aging=5 its effective priority reaches 1 after 45 ticks of
    waiting; it ties with the stream and wins on earlier arrival at t=46."""
    procs = [Proc(0, 0, [2], priority=10)] + [Proc(k + 1, 2 * k, [2], priority=1)
                                               for k in range(50)]
    starved, _ = run_procs(sched_bin, procs, algos="prio")
    aged, _ = run_procs(sched_bin, procs, algos="prio", aging=5)
    start = lambda res: next(p["start"] for p in res["prio"]["processes"] if p["pid"] == 0)
    assert start(starved) == 100
    assert start(aged) == 46
    assert aged["prio"]["summary"]["waiting"]["max"] < starved["prio"]["summary"]["waiting"]["max"]
    check_against_reference(procs, aged["prio"], aging=5)


def test_lottery_is_reproducible(sched_bin):
    procs = [Proc(p, 0, [20], tickets=t) for p, t in ((1, 100), (2, 200), (3, 300))]
    first, _ = run_procs(sched_bin, procs, algos="lottery", seed=42, quantum=1)
    again, _ = run_procs(sched_bin, procs, algos="lottery", seed=42, quantum=1)
    other, _ = run_procs(sched_bin, procs, algos="lottery", seed=43, quantum=1)
    assert first["lottery"]["gantt"] == again["lottery"]["gantt"]
    assert first["lottery"]["gantt"] != other["lottery"]["gantt"]
    check_against_reference(procs, first["lottery"], seed=42, quantum=1)


def cpu_share(result, window):
    ran = {}
    for s in result["gantt"]:
        if s["type"] == "run" and s["start"] < window:
            ran[s["pid"]] = ran.get(s["pid"], 0) + min(s["end"], window) - s["start"]
    return {pid: t / window for pid, t in ran.items()}


@pytest.mark.parametrize("seed", [1, 2, 3])
def test_lottery_share_converges_to_ticket_share(sched_bin, seed):
    """Three CPU-bound jobs with 100:200:300 tickets and quantum 1. Over the
    first 30,000 ticks (all three still runnable) each job's CPU share must be
    within 1 percentage point of its ticket share (about 3.4 standard
    deviations for the 50% job)."""
    procs = [Proc(p, 0, [30000], tickets=t) for p, t in ((1, 100), (2, 200), (3, 300))]
    got, _ = run_procs(sched_bin, procs, algos="lottery", seed=seed, quantum=1)
    share = cpu_share(got["lottery"], 30000)
    for pid, tickets in ((1, 100), (2, 200), (3, 300)):
        assert abs(share[pid] - tickets / 600) < 0.01, share


def test_stride_and_cfs_shares(sched_bin):
    """Deterministic proportional share: stride 3:1 tickets, CFS nice 0 vs 5
    (weights 1024:335)."""
    procs = [Proc(1, 0, [4000], tickets=300), Proc(2, 0, [4000], tickets=100)]
    got, _ = run_procs(sched_bin, procs, algos="stride", quantum=1)
    share = cpu_share(got["stride"], 4000)
    assert abs(share[1] - 0.75) < 0.001
    procs = [Proc(1, 0, [4000], nice=0), Proc(2, 0, [4000], nice=5)]
    got, _ = run_procs(sched_bin, procs, algos="cfs")
    share = cpu_share(got["cfs"], 4000)
    assert abs(share[1] - 1024 / 1359) < 0.01


# ---------------------------------------------------------------- opt-np

def brute_force_np(procs):
    best = None
    for order in itertools.permutations(procs):
        t = total = 0
        for pid, arrival, burst in order:
            t = max(t, arrival) + burst
            total += t - arrival
        best = total if best is None else min(best, total)
    return best


def test_opt_np_counterexample(sched_bin):
    """With staggered arrivals non-preemptive SJF is not optimal: P2 arrives at
    5 (burst 9), P1 at 6 (burst 2). SJF starts P2 at once (total turnaround
    9 + 10 = 19); idling one tick and running P1 first gives 2 + 12 = 14."""
    procs = [(2, 5, 9), (1, 6, 2)]
    got = run_json(sched_bin, procs, algos="sjf,opt-np", extra=["--gap"])
    assert total_turnaround(got["sjf"]) == 19
    assert total_turnaround(got["opt-np"]) == 14
    assert [(s["start"], s["end"], s["pid"]) for s in got["opt-np"]["gantt"]] == [
        (5, 6, None), (6, 8, 1), (8, 17, 2)]
    assert got["sjf"]["optimality_gap_pct"] == pytest.approx(100 * (19 - 14) / 14)
    assert got["opt-np"]["optimality_gap_pct"] == 0
    check_result([simple(*p) for p in procs], got["opt-np"])


def test_opt_np_matches_brute_force(sched_bin):
    rng = random.Random(8)
    for i in range(150):
        n = rng.randint(1, 8) if i % 5 else 8
        procs = [(pid, rng.randint(0, 12), rng.randint(1, 9)) for pid in rng.sample(range(1, 50), n)]
        got = run_json(sched_bin, procs, algos="opt-np")
        assert total_turnaround(got["opt-np"]) == brute_force_np(procs), procs
        check_result([simple(*p) for p in procs], got["opt-np"])


def test_opt_np_bounds_every_policy(sched_bin):
    """opt-np is never beaten by a non-preemptive policy, and never beats SRTF
    (the preemptive optimum), on random instances of up to 20 processes."""
    rng = random.Random(9)
    for _ in range(60):
        n = rng.randint(2, 20)
        procs = [(pid, rng.randint(0, 40), rng.randint(1, 12)) for pid in rng.sample(range(1, 99), n)]
        r = run_raw(sched_bin, ["--format=json", "--algo=all", "--gap", "--quantum=3"],
                    stdin_text(procs))
        assert r.returncode == 0, r.stderr
        doc = json.loads(r.stdout)
        gaps = {res["algorithm"]: res["optimality_gap_pct"] for res in doc["results"]}
        for alg in ("fcfs", "sjf", "prio", "hrrn"):
            assert gaps[alg] >= -1e-9, (alg, procs)
        assert gaps["srtf"] <= 1e-9, procs


def test_opt_np_refuses_unsupported_inputs(sched_bin):
    big = [(pid, pid, 1) for pid in range(21)]
    r = run_raw(sched_bin, ["--algo=opt-np", "--format=json"], stdin_text(big))
    assert r.returncode == 2 and "at most 20 processes (got 21)" in r.stderr
    small = [(1, 0, 2), (2, 1, 1)]
    for flags, message in ((["--cores=2"], "one core"), (["--cs-cost=1"], "--cs-cost=0")):
        r = run_raw(sched_bin, ["--algo=opt-np", "--format=json", *flags], stdin_text(small))
        assert r.returncode == 2 and message in r.stderr
    r = run_raw(sched_bin, ["--algo=fcfs", "--gap", "--format=json"], stdin_text(big))
    assert r.returncode == 2 and "opt-np" in r.stderr


# ---------------------------------------------------------------- realism

def test_cs_cost_changes_rr_but_zero_matches_default(sched_bin):
    procs = [(1, 0, 5), (2, 1, 3), (3, 2, 8), (4, 3, 6), (5, 4, 4)]
    default = run_json(sched_bin, procs, algos="rr")["rr"]
    zero = run_json(sched_bin, procs, algos="rr", extra=["--cs-cost=0"])["rr"]
    one = run_json(sched_bin, procs, algos="rr", extra=["--cs-cost=1"])["rr"]
    assert zero == default
    assert one["averages"]["turnaround"] > default["averages"]["turnaround"]
    assert one["summary"]["switch_time"] == one["summary"]["context_switches"] > 0
    check_result([simple(*p) for p in procs], one, cs_cost=1)


def test_ewma_prediction_reporting(sched_bin):
    """EWMA output reports MAE/MAPE (schedule independent, recomputed here)
    and each estimate-based policy's regret against the oracle."""
    procs = [Proc(1, 0, [8, 2, 2, 2, 8]), Proc(2, 1, [2, 1, 2, 1, 2]), Proc(3, 2, [5])]
    got, doc = run_procs(sched_bin, procs, algos="fcfs,sjf,srtf,hrrn", predict="ewma",
                         alpha=0.5, tau0=5.0)
    count, mae, mape = reference_sim.prediction_error(procs, 0.5, 5.0)
    assert doc["prediction"] == {"bursts": count, "mae": mae, "mape_pct": mape}
    oracle, _ = run_procs(sched_bin, procs, algos="sjf,srtf,hrrn")
    for alg in ("sjf", "srtf", "hrrn"):
        mean = got[alg]["averages"]["turnaround"]
        base = oracle[alg]["averages"]["turnaround"]
        assert got[alg]["prediction_regret_pct"] == pytest.approx(100 * (mean - base) / base)
        check_against_reference(procs, got[alg], predict="ewma", alpha=0.5, tau0=5.0)
    assert "prediction_regret_pct" not in got["fcfs"]


# ---------------------------------------------------------------- regressions

# Workloads on which the differential test once found the engine and the
# reference disagreeing (all involved prio-p on two cores with switch cost).
REGRESSIONS = [
    ([Proc(23, 0, [4], 2, 100, 15), Proc(26, 0, [5], 0, 50, -17), Proc(53, 0, [1], 0, 1, 19),
      Proc(13, 0, [2], 3, 300, -18), Proc(56, 0, [1], 1, 300, -19), Proc(4, 0, [5], 3, 1, -10)],
     dict(aging=5, cores=2, cs_cost=2)),
    ([Proc(20, 4, [2], 2), Proc(29, 0, [8], 1), Proc(26, 2, [1, 6, 1, 5, 3], 2),
      Proc(9, 2, [6, 3, 3, 3, 5], 4), Proc(48, 4, [8], 3), Proc(49, 0, [5], 3),
      Proc(53, 4, [4, 6, 3], 3)],
     dict(aging=3, cores=2, cs_cost=1, predict="ewma", alpha=0.8, tau0=2.5)),
    ([Proc(7, 0, [9], 1), Proc(56, 0, [6], 3), Proc(36, 0, [5], 0), Proc(40, 0, [1], 3),
      Proc(32, 0, [3], 4), Proc(43, 0, [3], 4)],
     dict(aging=5, cores=2, cs_cost=2)),
    # a livelock: with aging 1 and switch cost 1, re-deciding at the end of
    # every switch-in would switch back and forth forever
    ([Proc(1, 0, [3], 3), Proc(2, 0, [3], 3)], dict(aging=1, cs_cost=1)),
]


@pytest.mark.parametrize("procs,opts", REGRESSIONS)
def test_regressions(sched_bin, procs, opts):
    got, _ = run_procs(sched_bin, procs, algos="prio-p", **opts)
    check_against_reference(procs, got["prio-p"], **opts)
