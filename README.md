# Preemptive and Non-Preemptive CPU Scheduling Simulator

[![CI](https://github.com/sneharc16/Preemptive-and-Non-Preemptive-CPU-Scheduling-Simulator/actions/workflows/ci.yml/badge.svg)](https://github.com/sneharc16/Preemptive-and-Non-Preemptive-CPU-Scheduling-Simulator/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/sneharc16/Preemptive-and-Non-Preemptive-CPU-Scheduling-Simulator/branch/main/graph/badge.svg)](https://codecov.io/gh/sneharc16/Preemptive-and-Non-Preemptive-CPU-Scheduling-Simulator)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

A CPU scheduling simulator written in C11.

## Overview

This project implements and compares CPU scheduling algorithms:

- **FCFS (First Come First Served, FIFO)** - Non-preemptive
- **SJF (Shortest Job First)** - Non-preemptive
- **SRTF (Shortest Remaining Time First)** - Preemptive
- **Round Robin (RR)** - Preemptive with a time quantum
- **Priority** - non-preemptive and preemptive, with optional aging
- **HRRN (Highest Response Ratio Next)** - Non-preemptive
- **MLFQ (Multi-Level Feedback Queue)** - following OSTEP chapter 8
- **Lottery** and **Stride** - proportional share
- **CFS-lite** - a simplified model of the Linux Completely Fair Scheduler
- **opt-np** - the exact optimal non-preemptive schedule (offline baseline)

`--algo=classic` (the default) runs the first four; `--algo=all` runs every
online policy. The precise model is documented in [docs/model.md](docs/model.md).

## Features

- One event-driven simulation engine with a pluggable policy interface
- Per-process metrics (start, completion, response, waiting, turnaround) and averages
- Gantt charts (text) and an optional per-tick timeline
- Output as text, CSV or JSON (`docs/json-schema.md`)
- Input from stdin (interactive) or a CSV file
- Input validation with clear error messages and exit codes
- 64-bit time values with overflow checks
- Optional realism: multiple cores, context-switch cost, I/O bursts, and
  EWMA burst prediction with MAE/MAPE and regret against exact knowledge
- Extended metrics (`--metrics=full`): median/p95/p99/max, slowdown, Jain's
  fairness index, throughput, CPU utilisation, context switches
- `--gap`: each policy's distance from the exact non-preemptive optimum

## Build

Prerequisites: a C11 compiler (GCC or Clang), CMake >= 3.16, and Python 3
with `pytest` for the tests.

```bash
make            # builds ./sched (CMake Release build under build/)
make test       # runs all tests
make asan       # runs all tests under AddressSanitizer + UndefinedBehaviorSanitizer
make coverage   # line coverage report (requires gcovr)
```

Without make:

```bash
cmake -S . -B build && cmake --build build
./build/sched --help
```

## Usage

```
sched [--algo=all|fcfs,sjf,srtf,rr] [--quantum=Q] [--input=FILE]
      [--format=text|csv|json] [--csv=FILE|--no-csv] [--no-gantt] [--per-tick]
```

| Option | Meaning |
|--------|---------|
| `--algo=LIST` | Comma-separated algorithms: `fcfs sjf srtf rr prio prio-p hrrn mlfq lottery stride cfs opt-np`, or `classic` (default), or `all` |
| `--quantum=Q` | Time quantum for rr, lottery and stride, integer > 0 (default 2) |
| `--aging=K` | Priority aging: effective priority improves by 1 per K ticks waiting |
| `--mlfq-levels`, `--mlfq-quanta`, `--mlfq-allot`, `--mlfq-boost` | MLFQ configuration |
| `--seed=N` | Lottery random seed (results are reproducible) |
| `--cfs-latency`, `--cfs-min-gran` | CFS-lite parameters |
| `--cores=N` | Number of CPUs sharing one ready queue (default 1) |
| `--cs-cost=C` | Ticks per context switch between different processes (default 0) |
| `--predict=oracle\|ewma`, `--alpha`, `--tau0` | Burst knowledge for SJF/SRTF/HRRN |
| `--metrics=basic\|full` | Extended metrics in text and CSV output |
| `--gap` | Report the optimality gap against opt-np |
| `--summary-csv=FILE` | One row of aggregate metrics per algorithm |
| `--input=FILE` | Read processes from a CSV file instead of stdin |
| `--format=FMT` | Output format on stdout: `text` (default), `csv`, `json` |
| `--csv=FILE` | Also write per-process metrics to FILE |
| `--no-csv` | Do not write a CSV file (text mode writes `schedule_metrics.csv` by default) |
| `--no-gantt` | Omit Gantt charts from text output |
| `--per-tick` | Add a per-tick timeline to text output |

### Input

**CSV file** (`--input=FILE`): a header row naming the columns (any order),
then one process per line. Blank lines and lines starting with `#` are
ignored. Required: `pid`, `arrival`, and `burst` or `bursts`. `bursts`
describes alternating CPU and I/O phases, e.g. `5:3:2` (5 ticks of CPU, 3 of
I/O, 2 of CPU). Optional: `priority` (lower runs first, default 0), `tickets`
(default 100) and `nice` (-20..19, default 0).

```csv
pid,arrival,burst
1,0,5
2,1,3
3,2,8
4,3,6
5,4,4
```

**Interactive (stdin)**: the number of processes, then one `PID Arrival Burst`
line per process. The time quantum is given with `--quantum`, not on stdin.

```
5
1 0 5
2 1 3
3 2 8
4 3 6
5 4 4
```

Constraints: at least one process; PID >= 0 and unique; arrival >= 0;
burst > 0; quantum > 0.

### Example

```bash
./sched --input=examples/basic.csv --no-csv --no-gantt
```

```
FCFS (FIFO) Scheduling =>
FCFS Averages:
  Response:  8.20
  Waiting :  8.20
  Turnaround:13.40

SJF (Non-preemptive) Scheduling =>
SJF Averages:
  Response:  6.60
  Waiting :  6.60
  Turnaround:11.80

SRTF (Preemptive SJF) Scheduling =>
SRTF Averages:
  Response:  5.80
  Waiting :  6.40
  Turnaround:11.60

Round Robin Scheduling (q=2) =>
RoundRobin(q=2) Averages:
  Response:  2.80
  Waiting :  12.60
  Turnaround:17.80
```

With Gantt chart (`./sched --input=examples/basic.csv --no-csv --algo=srtf`):

```
Gantt — SRTF:
[0  ,1  ) P1   | [1  ,4  ) P2   | [4  ,8  ) P1   | [8  ,12 ) P5   | [12 ,18 ) P4   | [18 ,26 ) P3
```

## Algorithm Specifications

Ties are broken deterministically as listed. The newer policies (priority,
HRRN, MLFQ, lottery, stride, CFS-lite, opt-np) are specified in
[docs/model.md](docs/model.md).

### First Come First Served (FCFS / FIFO)
- **Type**: Non-preemptive
- **Logic**: Runs processes in order of arrival; ties by PID
- **Advantages**: Simple, no starvation
- **Disadvantages**: Convoy effect: short jobs wait behind long ones

### Shortest Job First (SJF)
- **Type**: Non-preemptive
- **Logic**: Picks the arrived process with the shortest burst; ties by arrival, then PID
- **Advantages**: Minimises average waiting/turnaround time when all processes arrive at the same time
- **Disadvantages**: Not optimal when processes arrive at different times; long jobs can starve; needs burst lengths in advance

### Shortest Remaining Time First (SRTF)
- **Type**: Preemptive
- **Logic**: Always runs the process with the least remaining time; re-evaluated at each arrival; ties by arrival, then PID
- **Advantages**: Minimises average waiting/turnaround time over all possible schedules
- **Disadvantages**: More context switches, long jobs can starve, needs burst lengths in advance

### Round Robin
- **Type**: Preemptive
- **Logic**: FIFO queue; each process runs for at most one quantum, then rejoins the back of the queue. Processes that arrive during or exactly at the end of a slice are queued before the preempted process.
- **Advantages**: Fair, good response time, no starvation
- **Disadvantages**: Performance depends heavily on the quantum

## Performance Metrics

- **Response time**: first time on the CPU − arrival
- **Turnaround time**: completion − arrival
- **Waiting time**: turnaround − burst

## Error Handling

Invalid input is reported on stderr with a specific message, for example
`sched: error: duplicate PID 1: every process needs a unique PID` or
`sched: error: line 3: burst of P2 must be > 0 (got 0)`.

| Exit code | Meaning |
|-----------|---------|
| 0 | Success |
| 1 | Invalid input (bad numbers, duplicate PIDs, empty input, overflow, ...) |
| 2 | Invalid command-line usage (unknown option, bad quantum, unknown algorithm) |
| 3 | A file could not be read or written |
| 4 | Out of memory |

## Testing

`make test` runs:

- C unit tests for every module
- Golden tests: output must match the original implementation's saved output
- Hand-computed textbook cases with expected Gantt charts
- Differential testing of every policy against an independent tick-by-tick
  reference simulator (`tools/reference_sim.py`) on 5,000 random workloads with
  random cores, switch costs, I/O and prediction settings, with invariant
  checks on every schedule
- Optimality checks: SJF against brute force when all processes arrive at t=0,
  SRTF against an exhaustive search over all preemptive schedules, and opt-np
  against brute force over all job orders
- Command-line and error-handling tests

## Project Structure

```
include/sched/   public headers
src/engine/      simulation engine and workload validation
src/policies/    one file per scheduling policy + registry
src/io/          input parsers and text/CSV/JSON output
src/metrics/     metric calculations
src/cli/         command-line front end
tests/           unit, golden, differential, optimality and CLI tests
tools/           reference simulator and golden-file generator
examples/        sample workloads
```

## Limitations

- Multiple cores share one ready queue; per-core queues, load balancing and
  cache effects are not modelled
- The I/O device has unlimited capacity
- CFS-lite is a simplified model of Linux CFS, not a reimplementation
- Time is measured in integer ticks
- The full Gantt chart is kept in memory and limited to 16,777,216 segments

## License

Released under the MIT License. See [LICENSE](LICENSE).
