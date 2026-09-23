"""Hand-computed textbook cases with the full expected Gantt chart.

Every expectation below was worked out on paper from the tie-break rules in
docs/DEVELOPING.md; none was copied from program output. Gantt entries are
(start, end, pid) with pid None for idle. Start and completion times are
implied by the chart and checked too.
"""
import pytest

from properties import check_result
from reference_sim import simple
from simrun import gantt_tuples, run_json


def implied_start_completion(gantt):
    out = {}
    for s, e, pid in gantt:
        if pid is not None:
            out[pid] = (out[pid][0] if pid in out else s, e)
    return out


README_CASE1 = [(1, 0, 5), (2, 1, 3), (3, 2, 8), (4, 3, 6), (5, 4, 4)]
FCFS_CASE1 = [(0, 5, 1), (5, 8, 2), (8, 16, 3), (16, 22, 4), (22, 26, 5)]

CASES = {
    # Timeline starts at the first arrival (t=2) and shows the idle gap [5,7).
    "idle_gaps": dict(
        procs=[(1, 2, 3), (2, 7, 2), (3, 8, 1)], quantum=2,
        expected={alg: [(2, 5, 1), (5, 7, None), (7, 9, 2), (9, 10, 3)]
                  for alg in ("fcfs", "sjf", "srtf", "rr")}),
    # README test case 2: everyone arrives at t=0.
    "simultaneous_arrivals": dict(
        procs=[(1, 0, 4), (2, 0, 2), (3, 0, 6), (4, 0, 3), (5, 0, 1)], quantum=1,
        expected={
            "fcfs": [(0, 4, 1), (4, 6, 2), (6, 12, 3), (12, 15, 4), (15, 16, 5)],
            "sjf": [(0, 1, 5), (1, 3, 2), (3, 6, 4), (6, 10, 1), (10, 16, 3)],
            "srtf": [(0, 1, 5), (1, 3, 2), (3, 6, 4), (6, 10, 1), (10, 16, 3)],
            "rr": [(0, 1, 1), (1, 2, 2), (2, 3, 3), (3, 4, 4), (4, 5, 5), (5, 6, 1), (6, 7, 2),
                   (7, 8, 3), (8, 9, 4), (9, 10, 1), (10, 11, 3), (11, 12, 4), (12, 13, 1),
                   (13, 16, 3)],
        }),
    "single_process": dict(
        procs=[(7, 4, 5)], quantum=2,
        expected={alg: [(4, 9, 7)] for alg in ("fcfs", "sjf", "srtf", "rr")}),
    # Equal bursts and arrivals: pid decides; input order must not matter.
    "identical_bursts": dict(
        procs=[(3, 0, 4), (1, 0, 4), (2, 0, 4)], quantum=2,
        expected={
            "fcfs": [(0, 4, 1), (4, 8, 2), (8, 12, 3)],
            "sjf": [(0, 4, 1), (4, 8, 2), (8, 12, 3)],
            "srtf": [(0, 4, 1), (4, 8, 2), (8, 12, 3)],
            "rr": [(0, 2, 1), (2, 4, 2), (4, 6, 3), (6, 8, 1), (8, 10, 2), (10, 12, 3)],
        }),
    "one_tick_bursts": dict(
        procs=[(1, 0, 1), (2, 0, 1), (3, 1, 1), (4, 5, 1)], quantum=3,
        expected={alg: [(0, 1, 1), (1, 2, 2), (2, 3, 3), (3, 5, None), (5, 6, 4)]
                  for alg in ("fcfs", "sjf", "srtf", "rr")}),
    # Quantum 10 exceeds every burst, so RR degenerates to FCFS.
    "quantum_exceeds_bursts": dict(
        procs=README_CASE1, quantum=10,
        expected={"fcfs": FCFS_CASE1, "rr": FCFS_CASE1}),
    # P3 arrives mid-slice (t=1) and P2 exactly at the slice end (t=2): both
    # queue ahead of the preempted P1.
    "rr_arrival_at_slice_boundary": dict(
        procs=[(1, 0, 4), (2, 2, 2), (3, 1, 1)], quantum=2,
        expected={"rr": [(0, 2, 1), (2, 3, 3), (3, 5, 2), (5, 7, 1)]}),
    # P2 (burst 2) arrives at 3 and preempts P1 (5 left). At t=5 P1 and P3
    # both have 5 left; P1 arrived earlier and goes first. SJF can't preempt.
    "srtf_late_short_job_preempts": dict(
        procs=[(1, 0, 8), (2, 3, 2), (3, 4, 5)], quantum=2,
        expected={
            "srtf": [(0, 3, 1), (3, 5, 2), (5, 10, 1), (10, 15, 3)],
            "sjf": [(0, 8, 1), (8, 10, 2), (10, 15, 3)],
        }),
    # Equal remaining time on arrival does not preempt: earlier arrival wins.
    "srtf_tie_keeps_running_process": dict(
        procs=[(1, 0, 4), (2, 2, 2)], quantum=2,
        expected={"srtf": [(0, 4, 1), (4, 6, 2)]}),
    # SJF tie on burst falls back to arrival, then pid.
    "sjf_tie_breaks": dict(
        procs=[(9, 0, 6), (4, 1, 2), (2, 2, 2), (3, 2, 2)], quantum=2,
        expected={"sjf": [(0, 6, 9), (6, 8, 4), (8, 10, 2), (10, 12, 3)]}),
}


@pytest.mark.parametrize("name", sorted(CASES))
def test_hand_computed(sched_bin, name):
    case = CASES[name]
    got = run_json(sched_bin, case["procs"], case["quantum"])
    for alg, expected in case["expected"].items():
        assert gantt_tuples(got[alg]) == expected, f"{name}/{alg}"
        rows = {r["pid"]: (r["start"], r["completion"]) for r in got[alg]["processes"]}
        assert rows == implied_start_completion(expected), f"{name}/{alg}"
        check_result([simple(*p) for p in case["procs"]], got[alg])


def test_readme_case1_fcfs_metrics(sched_bin):
    """Turnarounds 5, 7, 14, 19, 22 -> mean 13.4; waiting = response = 8.2."""
    fcfs = run_json(sched_bin, README_CASE1, algos="fcfs")["fcfs"]
    assert [r["turnaround"] for r in fcfs["processes"]] == [5, 7, 14, 19, 22]
    assert fcfs["averages"] == {"response": 8.2, "waiting": 8.2, "turnaround": 13.4}
