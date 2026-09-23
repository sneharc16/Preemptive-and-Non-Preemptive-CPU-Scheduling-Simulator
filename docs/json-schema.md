# JSON output format (`--format=json`)

`sched --format=json` writes one JSON document to stdout. `schema_version`
is bumped whenever a field is removed or changes meaning; new fields may be
added without a version bump, so consumers should ignore unknown keys.

All times are integer ticks (64-bit). Intervals are half-open: `[start, end)`.

Actual output of `printf '2\n1 0 3\n2 5 1\n' | sched --format=json --algo=rr --quantum=2`:

```json
{
  "schema_version": 1,
  "workload": [
    {"pid": 1, "arrival": 0, "burst": 3},
    {"pid": 2, "arrival": 5, "burst": 1}
  ],
  "results": [
    {
      "algorithm": "rr",
      "label": "RoundRobin(q=2)",
      "preemptive": true,
      "quantum": 2,
      "processes": [
        {"pid": 1, "arrival": 0, "burst": 3, "start": 0, "completion": 3, "response": 0, "waiting": 0, "turnaround": 3},
        {"pid": 2, "arrival": 5, "burst": 1, "start": 5, "completion": 6, "response": 0, "waiting": 0, "turnaround": 1}
      ],
      "averages": {"response": 0, "waiting": 0, "turnaround": 2},
      "makespan": 6,
      "gantt": [
        {"start": 0, "end": 3, "pid": 1},
        {"start": 3, "end": 5, "pid": null},
        {"start": 5, "end": 6, "pid": 2}
      ]
    }
  ]
}
```

## Top level

| Field            | Type    | Meaning |
|------------------|---------|---------|
| `schema_version` | integer | Currently `1`. |
| `workload`       | array   | The input processes, in input order: `pid`, `arrival`, `burst`. |
| `results`        | array   | One entry per selected policy, in canonical order (`fcfs`, `sjf`, `srtf`, `rr`). |

## `results[i]`

| Field        | Type            | Meaning |
|--------------|-----------------|---------|
| `algorithm`  | string          | Policy name as accepted by `--algo`. |
| `label`      | string          | Label used in CSV output, e.g. `FCFS`, `RoundRobin(q=2)`. |
| `preemptive` | boolean         | Whether the policy can take the CPU away from a running process. |
| `quantum`    | integer or null | Time quantum for quantum-based policies, otherwise `null`. |
| `processes`  | array           | Per-process metrics, in input order (see below). |
| `averages`   | object          | Arithmetic means of `response`, `waiting`, `turnaround` over all processes. Printed with enough digits to round-trip a double exactly. |
| `makespan`   | integer         | Latest completion time. |
| `gantt`      | array           | Timeline segments `{start, end, pid}`; `pid` is `null` when the CPU is idle. |

## `processes[j]`

| Field        | Definition |
|--------------|------------|
| `pid`, `arrival`, `burst` | Echo of the input. |
| `start`      | First time the process runs. |
| `completion` | Time its last tick finishes. |
| `response`   | `start - arrival` |
| `turnaround` | `completion - arrival` |
| `waiting`    | `turnaround - burst` |

## Gantt invariants

These hold for every result and are checked by `tests/properties.py`:

- Segments are sorted, non-empty, contiguous (`gantt[k].end == gantt[k+1].start`)
  and cover exactly `[first arrival, makespan]`.
- Neighbouring segments never have the same `pid` (consecutive slices of one
  process are merged).
- Each process appears for exactly `burst` ticks in total, never before its
  arrival; its first segment starts at `start` and its last ends at `completion`.
- An idle segment never overlaps a period in which some process had arrived
  but not completed (the simulated CPU is work-conserving).
