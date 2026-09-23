# Developing

Guidance for working in this repository. Read this first.

## What this is

A single-CPU scheduling simulator in C11 (`sched`) with FCFS, SJF, SRTF and
Round Robin, built around one event-driven engine and a pluggable policy
interface. Python is used only for tests and tools; the C core has no
dependencies.

## Build and test

```sh
make                 # CMake Release build, copies the binary to ./sched
make test            # build + all tests (C unit tests and Python suites via CTest)
make asan            # Debug build with ASan + UBSan, runs every test under it
make coverage        # gcov build + tests + gcovr report; fails below 90% line coverage of src/
make format          # clang-format (pinned to 23.1.1 in CI) over tracked C sources
make format-check    # what CI runs
make clean
```

Requirements: CMake >= 3.16, a C11 compiler, Python 3 with `pytest`
(`gcovr` for coverage, `clang-format==23.1.1` for formatting).

Single suite: `ctest --test-dir build -R py_cli --output-on-failure`, or
`SCHED_BIN=build/sched python3 -m pytest tests/test_cli.py -q`.
`SCHED_DIFF_N=500` shrinks the differential run while iterating (CI uses the
default 5000).

## Layout

```
include/sched/   public headers: core.h (Process, Workload, errors), checked.h,
                 policy.h (policy interface + registry), engine.h, metrics.h, io.h
src/engine/      engine.c (simulation loop, timeline), workload.c (validation)
src/policies/    one file per policy + registry.c
src/io/          input.c (interactive + CSV parsers), output.c (text/CSV/JSON)
src/metrics/     per-process and average metrics
src/util/        fixed-capacity heap and FIFO queue (private)
src/cli/main.c   argument parsing and orchestration
tests/unit/      C unit tests (tiny header-only framework in check.h)
tests/*.py       golden, hand-computed cases, CLI, differential, optimality
tests/golden/    baseline captured from the ORIGINAL main.c -- never regenerate
tools/           reference_sim.py (naive tick-by-tick oracle), make_golden.py
docs/            json-schema.md
examples/        sample workloads
```

## Architecture

The **engine** (`sim_run`) owns the clock, the timeline and per-process
accounting (`remaining`, start, completion). A **policy** only decides who runs
next and for how long. Protocol (see `include/sched/policy.h`):

1. The timeline starts at the first arrival. Before each decision the engine
   admits every process with `arrival <= now` via `on_arrival`, in
   `(arrival, pid)` order.
2. `next_slice` returns `(proc, length)` or false if nothing is ready (the
   engine then records an IDLE segment up to the next arrival). The chosen
   process leaves the policy's ready set.
3. The engine runs `min(length, remaining)` ticks, further capped at the next
   arrival if `preempt_on_arrival` is set (SRTF).
4. After the slice the engine FIRST admits arrivals `<= now`, THEN calls
   `on_complete` (optional) or `on_preempt`. This ordering is what gives RR its
   tie-break rule.

Adding a policy: create `src/policies/<name>.c` defining
`const Policy POLICY_<NAME>` and add `X(POLICY_<NAME>)` to the table in
`src/policies/registry.c`. CMake picks up the file by glob; `--algo=<name>`
works automatically. Ready structures can be fixed-capacity (`view->n`)
because a process is never in the ready set twice.

## Scheduling semantics (must not change)

Tie-breaks, all verified against `tools/reference_sim.py`:

- **FCFS**: `(arrival, pid)`.
- **SJF** (non-preemptive): `(burst, arrival, pid)`.
- **SRTF**: `(remaining, arrival, pid)`, re-evaluated at every arrival; an
  arrival with remaining equal to the running process does not preempt it
  when the running process arrived earlier.
- **RR**: one FIFO. Processes arriving during a slice or exactly when it ends
  are queued BEFORE the preempted process is re-queued. Consecutive slices of
  the same process are merged in the Gantt chart.

Other invariants:

- All times are `int64_t`. `workload_validate` rejects empty workloads,
  `pid < 0`, `arrival < 0`, `burst <= 0`, duplicate PIDs, and any workload
  where `max(arrival) + sum(burst)` would overflow int64 -- this bound makes all
  engine arithmetic safe.
- The timeline is stored in full and capped at `SCHED_DEFAULT_MAX_SEGMENTS`
  (2^24) segments; beyond that `sim_run` fails with a clear error rather than
  exhausting memory.
- No global mutable state in `src/` (no `strtok`, no qsort context globals).
- Averages are summed in `double` in input order (matches the original output).

## Rules

- **Golden tests must stay green.** `tests/golden/` holds the output of the
  original single-file program. CSV must match byte for byte; text must match
  byte for byte except that the original FCFS chart began with an IDLE segment
  at t=0 when the first arrival was later (every policy now starts at the first
  arrival; `tests/test_golden.py` strips that one segment). Never regenerate the
  golden files from the new binary.
- Never change the behaviour of the four original policies. If a semantic looks
  wrong, raise it instead of changing it.
- Every new feature needs: hand-computed cases, reference-simulator support,
  and property checks (`tests/properties.py`).
- Build must stay warning-free with `-std=c11 -Wall -Wextra -Wpedantic -Werror`
  on gcc and clang, and the full suite must pass under ASan + UBSan.
- Never invent numbers in docs or commit messages; run the code.
- Conventional commit messages; small commits.

## CLI summary

```
sched [--algo=all|fcfs,sjf,srtf,rr] [--quantum=Q] [--input=FILE]
      [--format=text|csv|json] [--csv=FILE|--no-csv] [--no-gantt] [--per-tick]
```

- Without `--input`, reads the legacy interactive format from stdin
  (`N`, then `N` lines of `pid arrival burst`); prompts are printed only in text format.
- `--input=FILE` reads CSV with a header naming `pid,arrival,burst` (any order,
  case-insensitive; blank lines and `#` comments allowed; unknown columns are
  rejected). Add optional columns to the `COLUMNS` table in `src/io/input.c`.
- Text format writes `schedule_metrics.csv` unless `--no-csv` (legacy
  behaviour); csv/json formats write a CSV file only with an explicit `--csv=FILE`.
- Exit codes: 0 ok, 1 invalid input, 2 usage, 3 I/O, 4 out of memory.
