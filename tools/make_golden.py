#!/usr/bin/env python3
"""Generate the regression baseline in tests/golden/ from a given binary.

This was run ONCE against the original single-file main.c (commit 6f4fa49,
built with `cc -O2 -std=c11 main.c -o sched_v1`) before any refactoring.
The golden files are the proof that the refactor did not change behaviour;
do not regenerate them from the new binary.

Usage: tools/make_golden.py PATH/TO/sched_v1
"""
import os
import random
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GOLDEN = os.path.join(ROOT, "tests", "golden")
QUANTA = (1, 2, 3, 5)

README_CASES = {
    "readme_case1": [(1, 0, 5), (2, 1, 3), (3, 2, 8), (4, 3, 6), (5, 4, 4)],
    "readme_case2": [(1, 0, 4), (2, 0, 2), (3, 0, 6), (4, 0, 3), (5, 0, 1)],
}


def random_cases(count=50, seed=20260923):
    rng = random.Random(seed)
    cases = {}
    for i in range(count):
        n = rng.randint(1, 12) if i < 45 else rng.randint(20, 40)
        pids = rng.sample(range(1, 100), n)
        procs = [(p, rng.randint(0, 30), rng.randint(1, 15)) for p in pids]
        cases[f"random_{i:02d}"] = procs
    return cases


def stdin_text(procs):
    return f"{len(procs)}\n" + "".join(f"{p} {a} {b}\n" for p, a, b in procs)


def run(binary, procs, args):
    with tempfile.TemporaryDirectory() as tmp:
        r = subprocess.run([binary, "--csv=out.csv", *args], input=stdin_text(procs),
                           capture_output=True, text=True, cwd=tmp, check=True)
        with open(os.path.join(tmp, "out.csv")) as f:
            return r.stdout, f.read()


def main():
    binary = os.path.abspath(sys.argv[1])
    cases = {**README_CASES, **random_cases()}
    os.makedirs(os.path.join(GOLDEN, "inputs"), exist_ok=True)
    os.makedirs(os.path.join(GOLDEN, "expected"), exist_ok=True)
    for name, procs in cases.items():
        with open(os.path.join(GOLDEN, "inputs", f"{name}.in"), "w") as f:
            f.write(stdin_text(procs))
        for q in QUANTA:
            out, csv = run(binary, procs, [f"--quantum={q}"])
            base = os.path.join(GOLDEN, "expected", f"{name}.q{q}")
            with open(base + ".txt", "w") as f:
                f.write(out)
            with open(base + ".csv", "w") as f:
                f.write(csv)
    out, csv = run(binary, README_CASES["readme_case1"], ["--quantum=2", "--per-tick"])
    with open(os.path.join(GOLDEN, "expected", "readme_case1.q2.pertick.txt"), "w") as f:
        f.write(out)
    print(f"wrote {len(cases)} inputs x {len(QUANTA)} quanta (+1 per-tick) to {GOLDEN}")


if __name__ == "__main__":
    main()
