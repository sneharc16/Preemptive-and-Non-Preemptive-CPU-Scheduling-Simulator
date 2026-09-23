# JSON output format (`--format=json`)

`sched --format=json` writes one JSON document to stdout. `schema_version`
is bumped whenever a field is removed or changes meaning; new fields may be
added without a version bump, so consumers should ignore unknown keys.

All times are integer ticks (64-bit). Intervals are half-open: `[start, end)`.

Actual output for the workload

```csv
pid,arrival,bursts
1,0,2:3:1
2,1,2
```

run with `sched --input=w.csv --algo=rr --quantum=2 --cs-cost=1 --format=json`
(P1 computes for 2 ticks, does 3 ticks of I/O, then computes for 1 tick;
every switch between different processes costs 1 tick):

```json
{
  "schema_version": 1,
  "workload": [
    {"pid": 1, "arrival": 0, "burst": 3, "priority": 0, "tickets": 100, "nice": 0, "bursts": [2, 3, 1]},
    {"pid": 2, "arrival": 1, "burst": 2, "priority": 0, "tickets": 100, "nice": 0, "bursts": [2]}
  ],
  "options": {"cores": 1, "cs_cost": 1, "predict": "oracle"},
  "results": [
    {
      "algorithm": "rr",
      "label": "RoundRobin(q=2)",
      "preemptive": true,
      "quantum": 2,
      "processes": [
        {"pid": 1, "arrival": 0, "burst": 3, "io": 3, "start": 0, "completion": 7, "response": 0, "waiting": 1, "turnaround": 7, "slowdown": 2.3333333333333335},
        {"pid": 2, "arrival": 1, "burst": 2, "io": 0, "start": 3, "completion": 5, "response": 2, "waiting": 2, "turnaround": 4, "slowdown": 2}
      ],
      "averages": {"response": 1, "waiting": 1.5, "turnaround": 5.5},
      "summary": {
        "turnaround": {"mean": 5.5, "median": 5.5, "p95": 7, "p99": 7, "max": 7},
        "waiting": {"mean": 1.5, "median": 1.5, "p95": 2, "p99": 2, "max": 2},
        "response": {"mean": 1, "median": 1, "p95": 2, "p99": 2, "max": 2},
        "slowdown_mean": 2.166666666666667, "slowdown_max": 2.3333333333333335, "jain_fairness_slowdown": 0.99411764705882355,
        "span": 7, "throughput": 0.2857142857142857, "cpu_utilization": 0.7142857142857143,
        "context_switches": 2, "switch_time": 2, "switch_fraction": 0.2857142857142857
      },
      "makespan": 7,
      "cores": 1,
      "gantt": [
        {"start": 0, "end": 2, "pid": 1, "type": "run", "core": 0},
        {"start": 2, "end": 3, "pid": 2, "type": "cs", "core": 0},
        {"start": 3, "end": 5, "pid": 2, "type": "run", "core": 0},
        {"start": 5, "end": 6, "pid": 1, "type": "cs", "core": 0},
        {"start": 6, "end": 7, "pid": 1, "type": "run", "core": 0}
      ]
    }
  ]
}
```

## Top level

| Field            | Type    | Meaning |
|------------------|---------|---------|
| `schema_version` | integer | Currently `1`. |
| `workload`       | array   | The input processes in input order: `pid`, `arrival`, total CPU `burst`, `priority`, `tickets`, `nice`, and `bursts` (the phases cpu, io, cpu, ..., cpu). |
| `options`        | object  | Machine model: `cores`, `cs_cost`, `predict` (`"oracle"` or `"ewma"`, with `alpha` and `tau0` for EWMA). |
| `prediction`     | object  | Only with `--predict=ewma`: `bursts` predicted, `mae` (ticks) and `mape_pct` over every CPU burst. Independent of the policy. |
| `optimum_mean_turnaround` | number | Only with `--gap`: opt-np's mean turnaround. |
| `results`        | array   | One entry per selected policy, in registry order (`fcfs`, `sjf`, `srtf`, `rr`, `prio`, `prio-p`, `hrrn`, `mlfq`, `lottery`, `stride`, `cfs`, `opt-np`). |

## `results[i]`

| Field        | Type            | Meaning |
|--------------|-----------------|---------|
| `algorithm`  | string          | Policy name as accepted by `--algo`. |
| `label`      | string          | Label used in CSV output, e.g. `FCFS`, `RoundRobin(q=2)`. |
| `preemptive` | boolean         | Whether the policy can take the CPU away from a process mid-burst. |
| `quantum`    | integer or null | Time quantum for quantum-based policies (rr, lottery, stride), otherwise `null`. |
| `processes`  | array           | Per-process metrics, in input order (see below). |
| `averages`   | object          | Means of `response`, `waiting`, `turnaround`. Numbers are printed with enough digits to round-trip a double. |
| `summary`    | object          | Aggregate metrics (see below). |
| `optimality_gap_pct` | number  | Only with `--gap`: `100 * (mean turnaround - opt-np mean) / opt-np mean`. Preemptive policies can be negative. |
| `prediction_regret_pct` | number | Only with `--predict=ewma`, for policies that use burst estimates (sjf, srtf, hrrn): `100 * (mean turnaround with EWMA - with oracle) / with oracle`. |
| `makespan`   | integer         | Latest completion time. |
| `cores`      | integer         | Number of CPUs. |
| `gantt`      | array           | Timeline segments `{start, end, pid, type, core}`, core 0 first. `type` is `run`, `idle` (`pid` is `null`) or `cs` (a context switch into `pid`). |

## `processes[j]`

| Field        | Definition |
|--------------|------------|
| `pid`, `arrival` | Echo of the input. |
| `burst`, `io` | Total CPU time and total I/O time. |
| `start`      | First tick on a CPU (after any switch-in). |
| `completion` | End of the last CPU tick. |
| `response`   | `start - arrival` |
| `turnaround` | `completion - arrival` |
| `waiting`    | `turnaround - burst - io`: time ready but not running, including switch-in time. |
| `slowdown`   | `turnaround / burst` |

## `summary`

| Field | Definition |
|-------|------------|
| `turnaround`, `waiting`, `response` | `{mean, median, p95, p99, max}` over processes. Percentiles use the nearest-rank method (the smallest value with at least p% of values at or below it); the median averages the middle pair when n is even. |
| `slowdown_mean`, `slowdown_max` | Over per-process slowdown. |
| `jain_fairness_slowdown` | Jain's index `(sum x)^2 / (n * sum x^2)` over per-process slowdown; 1 means every process was slowed down equally. |
| `span` | `makespan - first arrival`. |
| `throughput` | Processes completed per tick over the span. |
| `cpu_utilization` | CPU ticks spent running processes / (`cores * span`). |
| `context_switches` | Dispatches onto a core whose previous process was a different one (counted even when `cs_cost` is 0). |
| `switch_time`, `switch_fraction` | Core-ticks spent switching, absolute and as a fraction of `cores * span`. |

## Gantt invariants

These hold for every result and are checked by `tests/properties.py`:

- Each core's segments are sorted, non-empty, contiguous
  (`gantt[k].end == gantt[k+1].start`) and cover exactly `[first arrival, makespan]`.
- Neighbouring segments on a core never share both `type` and `pid`.
- A `cs` segment lasts exactly `cs_cost` ticks.
- A process is never on two cores at once, runs for exactly its CPU bursts
  (never before arrival, never during its I/O), and its first and last run
  ticks are `start` and `completion`.
- Except for `opt-np` (which may idle on purpose), no core is idle while a
  process is ready.
