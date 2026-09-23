# Contributing

Thanks for your interest. This project values correctness over features:
every change must keep the existing tests green and come with tests of its
own.

## Setup

- C11 compiler (gcc or clang), CMake >= 3.16, Python 3 with `pytest`.
- Optional: `gcovr` (coverage), `clang-format==23.1.1` (formatting),
  `matplotlib` (experiments), Emscripten (browser build).

```sh
make            # build ./sched
make test       # all tests
make asan       # all tests under AddressSanitizer + UndefinedBehaviorSanitizer
make coverage   # fails below 90% line coverage of src/
make format     # clang-format the C sources
```

## Rules

1. **Golden tests stay green.** `tests/golden/` is the output of the original
   program; never regenerate it. Default behaviour must not change; new
   behaviour goes behind new flags.
2. **Every behaviour change has tests:** at least one case worked out by hand,
   support in `tools/reference_sim.py` so the differential test covers it, and
   any new invariant in `tests/properties.py`.
3. **Warning-free** with `-std=c11 -Wall -Wextra -Wpedantic -Werror` on gcc
   and clang, and clean under ASan + UBSan.
4. **No global mutable state** in `src/`.
5. **No invented numbers** in docs: figures come from `docs/results/`, which
   `make experiments` regenerates.
6. [Conventional commits](https://www.conventionalcommits.org/) (`feat:`,
   `fix:`, `perf:`, `test:`, `docs:`, ...), small and focused.

## Adding a scheduling policy

1. Create `src/policies/<name>.c` defining `const Policy POLICY_<NAME>`
   (see `include/sched/policy.h` for the protocol and `fcfs.c` for the
   smallest example). Size ready structures to `view->n` in `create`.
2. Add `X(POLICY_<NAME>)` to the table in `src/policies/registry.c`.
3. Implement the same policy in `tools/reference_sim.py` (plain lists, no
   clever data structures) and add its name to `ONLINE` in `tests/simrun.py`.
4. Add hand-computed cases to `tests/test_policies.py`.
5. Document its rules and tie-breaks in `docs/model.md`.
6. Run the differential test with a few extra seeds:
   `SCHED_DIFF_SEED=7 SCHED_DIFF_N=20000 SCHED_BIN=build/sched python3 -m pytest tests/test_differential.py`.

## Reporting a bug

Please include the workload (CSV), the exact command line, the output you got
and the output you expected. If you can, reproduce it with
`tools/reference_sim.py` too.
