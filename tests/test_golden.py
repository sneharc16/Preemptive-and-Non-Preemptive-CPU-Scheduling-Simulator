"""Regression test against the output of the original single-file main.c.

tests/golden/ was produced by tools/make_golden.py from the unmodified
program before the refactor. CSV output (start, completion and all metrics
for every algorithm) must match byte for byte. Text output must match byte
for byte too, with one documented exception: the original FCFS Gantt chart
began at t=0 with an IDLE segment when the first arrival was later, while
SJF/SRTF/RR (and now every policy) start the timeline at the first arrival.
That leading segment is removed from the expected text before comparing.
"""
import glob
import os
import re
import subprocess
import tempfile

import pytest

GOLDEN = os.path.join(os.path.dirname(os.path.abspath(__file__)), "golden")
QUANTA = (1, 2, 3, 5)
CASES = sorted(os.path.basename(p)[:-3] for p in glob.glob(os.path.join(GOLDEN, "inputs", "*.in")))

FCFS_LEADING_IDLE = re.compile(r"(Gantt — FCFS:\n)\[0  ,[^)]*\) IDLE  \| ")


def read(path):
    with open(path, newline="") as f:
        return f.read()


def run_legacy(binary, name, args):
    stdin = read(os.path.join(GOLDEN, "inputs", f"{name}.in"))
    with tempfile.TemporaryDirectory() as tmp:
        r = subprocess.run([binary, "--csv=out.csv", *args], input=stdin, capture_output=True,
                           text=True, cwd=tmp)
        assert r.returncode == 0, r.stderr
        assert r.stderr == ""
        return r.stdout, read(os.path.join(tmp, "out.csv"))


def test_golden_inventory():
    assert len(CASES) == 52
    assert len(glob.glob(os.path.join(GOLDEN, "expected", "*"))) == 52 * len(QUANTA) * 2 + 1


@pytest.mark.parametrize("quantum", QUANTA)
@pytest.mark.parametrize("name", CASES)
def test_golden(sched_bin, name, quantum):
    out, csv = run_legacy(sched_bin, name, [f"--quantum={quantum}"])
    base = os.path.join(GOLDEN, "expected", f"{name}.q{quantum}")
    assert csv == read(base + ".csv")
    expected_text = FCFS_LEADING_IDLE.sub(r"\1", read(base + ".txt"))
    assert out == expected_text


def test_golden_per_tick(sched_bin):
    out, _ = run_legacy(sched_bin, "readme_case1", ["--quantum=2", "--per-tick"])
    assert out == read(os.path.join(GOLDEN, "expected", "readme_case1.q2.pertick.txt"))
