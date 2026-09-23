# Developing

Guidance for working in this repository. Read this first.

## What this is

A CPU scheduling simulator in C11 (`sched`): one event-driven engine, a
pluggable policy interface, and 12 policies (fcfs, sjf, srtf, rr, prio,
prio-p, hrrn, mlfq, lottery, stride, cfs, and the offline exact baseline
opt-np). Optional realism: multiple cores, context-switch cost, I/O phases and
EWMA burst prediction. Python is used only for tests and tools; the C core has
no dependencies. `docs/model.md` is the precise specification of the model.

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

Single suite: `ctest --test-dir build -R py_policies --output-on-failure`, or
`SCHED_BIN=build/sched python3 -m pytest tests/test_policies.py -q`.
Differential test knobs: `SCHED_DIFF_N` (default 5000) and `SCHED_DIFF_SEED`.
Run a few extra seeds at 20000 after touching the engine or a policy.

## Layout

```
include/sched/   public headers: core.h (Process, Workload, errors), checked.h,
                 policy.h (policy interface, params, registry), engine.h
                 (SimOptions, segments, sim_run), metrics.h, io.h
src/engine/      engine.c (event loop, cores, switches, I/O, prediction), workload.c
src/policies/    one file per policy (priority.c holds prio and prio-p) + registry.c
src/io/          input.c (interactive + CSV parsers), output.c (text/CSV/JSON)
src/metrics/     per-process metrics, summaries, prediction error
src/util/        fixed-capacity heap and FIFO queue (private)
src/cli/main.c   argument parsing and orchestration (--gap, regret, files)
tests/unit/      C unit tests (tiny header-only framework in check.h)
tests/*.py       golden, cases, cli, differential, optimality, policies
tests/golden/    baseline captured from the ORIGINAL main.c -- never regenerate
tools/           reference_sim.py (naive tick-by-tick oracle), make_golden.py
docs/            model.md (the model), json-schema.md (JSON output)
examples/        sample workloads
```

## Architecture

The **engine** (`sim_run`) owns the clock, the cores, the timeline, I/O and
per-process accounting (remaining time of the current CPU burst, predictions).
A **policy** only decides who runs next and for how long. The protocol,
including the exact order of hooks within one instant, is documented at the
top of `include/sched/policy.h`; `docs/model.md` restates it. Key points:

- Hooks: `on_arrival`, `on_ready` (I/O done), `next_slice` -> `(proc, length,
  deadline)`, `on_preempt(p, ran)`, `on_block(p, ran)`, `on_complete(p, ran)`.
- Arrivals and I/O completions are admitted before slice outcomes at the same
  instant (this gives RR its tie-break).
- `preempt_on_ready` policies (srtf, prio-p, mlfq) have every running slice
  interrupted whenever the ready set gains a process or a deadline falls due,
  so the running set is re-chosen globally (important with several cores).
- A slice `deadline` lets a policy re-decide at a time of its choosing
  (prio-p aging steps, MLFQ boosts).
- A switch-in is atomic and is followed by at least one tick of running.
- `work_conserving = false` lets a policy idle on purpose (opt-np only);
  `offline = true` keeps it out of `--algo=all`.

Adding a policy: create `src/policies/<name>.c` defining
`const Policy POLICY_<NAME>` and add `X(POLICY_<NAME>)` to the table in
`src/policies/registry.c`. CMake picks up the file by glob. Then add the same
policy to `tools/reference_sim.py`, hand-computed cases to
`tests/test_policies.py`, and its name to `ONLINE` in `tests/simrun.py`.

## Scheduling semantics (must not change)

Original four, verified against the golden baseline and the reference:

- **FCFS**: `(arrival, pid)`.
- **SJF** (non-preemptive): `(burst, arrival, pid)`.
- **SRTF**: `(remaining, arrival, pid)`; an arrival with remaining equal to the
  running process does not preempt it when the running process arrived earlier.
- **RR**: one FIFO; processes arriving during a slice or exactly when it ends
  are queued BEFORE the preempted process is re-queued.

New policies: see `docs/model.md` (tie-breaks, aging, MLFQ rules 1-5 and the
interrupted-job-to-front rule, lottery PRNG, stride joining rule, CFS-lite
formulas, opt-np limits).

Other invariants:

- All times are `int64_t`. `workload_validate` rejects empty workloads,
  `pid < 0`, `arrival < 0`, non-positive bursts, even phase counts, tickets
  outside 1..2^20, nice outside -20..19, duplicate PIDs, and any workload where
  `max(arrival) + sum(all phases)` would overflow. `sim_run` additionally
  bounds `cs_cost` so that switch overhead cannot overflow.
- The timeline is capped at `SCHED_DEFAULT_MAX_SEGMENTS` (2^24) segments over
  all cores; beyond that `sim_run` fails with a clear error.
- No global mutable state in `src/`.
- Averages are summed in `double` in input order (matches the original output).
- Build uses `-ffp-contract=off`; EWMA arithmetic must stay
  `alpha * actual + (1 - alpha) * tau` so C and Python agree bit for bit.

## Rules

- **Golden tests must stay green.** `tests/golden/` holds the output of the
  original single-file program. With default options, CSV must match byte for
  byte and text must match byte for byte except that the original FCFS chart
  began with an IDLE segment at t=0 when the first arrival was later
  (`tests/test_golden.py` strips that one segment). Never regenerate the
  golden files from the new binary. New output only appears behind new flags.
- Never change the behaviour of existing policies at default options. If a
  semantic looks wrong, raise it instead of changing it.
- Every new feature needs: hand-computed cases, reference-simulator support,
  and property checks (`tests/properties.py`).
- Build must stay warning-free with `-std=c11 -Wall -Wextra -Wpedantic -Werror`
  on gcc and clang, and the full suite must pass under ASan + UBSan.
- Never invent numbers in docs or commit messages; run the code.
- Conventional commit messages; small commits.

## CLI summary

```
sched [--algo=LIST] [--quantum=Q] [--input=FILE] [--format=text|csv|json]
      [--metrics=basic|full] [--gap] [--csv=FILE|--no-csv] [--summary-csv=FILE]
      [--no-gantt] [--per-tick]
      [--cores=N] [--cs-cost=C] [--predict=oracle|ewma] [--alpha=A] [--tau0=T]
      [--aging=K] [--mlfq-levels=L] [--mlfq-quanta=LIST] [--mlfq-allot=LIST]
      [--mlfq-boost=S] [--seed=N] [--cfs-latency=L] [--cfs-min-gran=G]
```

- `--algo`: comma-separated names, `classic` (the default: fcfs,sjf,srtf,rr)
  or `all` (every online policy; opt-np only when named).
- Without `--input`, reads the legacy interactive format from stdin
  (`N`, then `N` lines of `pid arrival burst`); prompts are printed only in text format.
- `--input=FILE` reads CSV with a header: `pid`, `arrival`, and `burst` and/or
  `bursts` (`cpu:io:...:cpu`), plus optional `priority`, `tickets`, `nice`
  (any order, case-insensitive; blank lines and `#` comments allowed; unknown
  columns rejected; empty optional fields take defaults). Columns live in the
  `COLUMNS` table in `src/io/input.c`.
- Text format writes `schedule_metrics.csv` unless `--no-csv` (legacy
  behaviour); csv/json formats write a CSV file only with an explicit `--csv=FILE`.
- Exit codes: 0 ok, 1 invalid input, 2 usage (including unsupported option
  combinations such as opt-np with more than 20 processes), 3 I/O, 4 out of memory.
