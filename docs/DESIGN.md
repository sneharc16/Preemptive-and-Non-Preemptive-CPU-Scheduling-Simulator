# Design

This document explains how the simulator is built and why. The exact
scheduling semantics are specified separately in [model.md](model.md); the
JSON output format in [json-schema.md](json-schema.md).

## Goals

1. **Correct beyond reasonable doubt.** A scheduling simulator is only useful
   if its schedules are right, and most bugs hide in rare coincidences (an
   arrival at the exact end of a slice, two cores freeing at once). The design
   favours structures that make these cases explicit and testable.
2. **One engine, many policies.** Adding a policy must not mean touching the
   clock, the metrics or the output.
3. **Deterministic and portable.** The same input gives byte-identical output
   on Linux, macOS, gcc, clang and WebAssembly.
4. **Fast enough for a million processes**, so experiments can use realistic
   workload sizes.

## Layers

```
src/cli        argument parsing; orchestrates runs, --gap, EWMA regret
src/io         input parsers (CSV, legacy interactive) and writers (text, CSV, JSON)
src/engine     workload validation; the event-driven simulation engine
src/policies   one file per policy, plus the registry table
src/metrics    per-process metrics, distributions, fairness, prediction error
src/util       fixed-capacity binary heap and FIFO ring buffer
```

Dependencies only point downwards (cli -> io/engine/metrics -> policies ->
util). There is no global mutable state anywhere in `src/`, so the browser
build can call `main()` repeatedly in one process.

## The engine

`sim_run` owns everything that is not a decision: the clock, the cores (each
free, switching in, or running a slice), the timeline, the blocked set
(a heap of I/O completion times), each process's current CPU phase and
remaining time, and EWMA predictions.

It is a discrete-event loop. Events are arrivals, I/O completions, the end of
a switch-in, the end of a run slice, and policy deadlines. At each instant it
processes them in a fixed order (arrivals, I/O completions, finished slices,
finished switches, interruptions, then dispatch to free cores); that order is
the specification, written out at the top of `include/sched/policy.h`.

Two rules make multi-core and switch costs well defined:

- **Global re-decision.** For policies that react to new work (SRTF,
  preemptive priority, MLFQ), every running slice is interrupted whenever the
  ready set gains a process or a deadline falls due, and the policy chooses the
  whole running set again. A process that is re-chosen stays on its core, so no
  switch is charged. Without this, a process handed back on core 1 could be
  better than one running on core 0 and nothing would notice; the differential
  test found exactly that.
- **Atomic switch-in plus one tick.** A context switch cannot be interrupted,
  and the switched-in process always runs at least one tick before a new
  decision takes effect. Re-deciding the moment a switch ends can livelock:
  with aging, the waiting process overtakes the incoming one during every
  switch, so the CPU switches back and forth forever. The differential test
  found that too.

Timeline segments are merged as they are appended, and their total count is
capped (2^24 by default) so that a pathological input fails with a clear
message instead of exhausting memory.

## The policy interface

```c
typedef struct Policy {
    const char *name, *label, *heading;
    bool preemptive, preempt_on_ready, uses_quantum, uses_estimates;
    bool work_conserving, offline;
    void *(*create)(const SchedView *, const PolicyParams *, SchedError *);
    void  (*destroy)(void *);
    void  (*on_arrival)(void *, size_t proc);
    void  (*on_ready)(void *, size_t proc);            /* I/O completed */
    bool  (*next_slice)(void *, Slice *out);           /* who, how long, deadline */
    void  (*on_preempt)(void *, size_t proc, int64_t ran);
    void  (*on_block)(void *, size_t proc, int64_t ran);
    void  (*on_complete)(void *, size_t proc, int64_t ran);
} Policy;
```

A policy sees a read-only `SchedView` (the processes, the current time,
remaining and expected burst lengths) and keeps its own ready structure. It
never advances time or computes metrics. `Slice.deadline` lets time-driven
policies (priority aging, MLFQ boosts) ask to be consulted at a particular
instant without the engine knowing why.

Ready structures can be sized to the number of processes up front because a
process is never in a ready set twice, so no policy allocates memory while
the simulation runs.

## Data structures per policy

| Policy | Structure | Per decision |
|---|---|---|
| FCFS, RR | ring buffer | O(1) |
| SJF, SRTF, stride, CFS-lite | binary heap with a comparator over the view | O(log n) |
| Priority (with and without aging) | treap keyed by `c mod K` with subtree minima | O(log n) expected |
| HRRN | processes grouped by burst length; a queue per group | O(groups with ready processes) |
| MLFQ | one doubly linked queue per level | O(levels) |
| Lottery | Fenwick tree of tickets over pid ranks | O(log n) |
| opt-np | branch and bound, computed once at creation | exponential worst case, n ≤ 20 |

**Priority with aging.** Effective priority is `priority - floor(W / K)`,
where W, the time waited, grows while a process is queued, so the order changes
over time. With `W(now) = c + now` for a per-process constant c and
`c = q*K + r`, the effective priority equals
`(priority - q) - floor(now / K) - [r >= K - now mod K]`. The middle term is
shared, so the order depends only on the fixed value `priority - q` and on
which side of a moving threshold r falls. A treap ordered by r, with each node
storing the best `(priority - q, arrival, pid)` in its subtree, answers "best
with r below T" and "best with r at or above T" in O(log n); the next aging
step comes from the largest r on either side of the threshold. This replaced a
linear scan that made overloaded runs quadratic (see
[results/perf](results/perf/before_after.md)).

**HRRN.** Among processes with the same expected burst, the one that became
ready first has the largest response ratio, so only the head of each group can
win. Ratios are compared as exact fractions (a continued-fraction style
comparison that cannot overflow), so ties are real ties.

**opt-np.** Minimising total turnaround without preemption, with release
dates, is strongly NP-hard. The search picks the next job to run; each job
starts at `max(previous completion, arrival)`, which covers deliberate idling.
It prunes with the SRPT (preemptive) optimum of the remaining jobs as a lower
bound, with dominance between partial schedules over the same job set (finish
no later and total no larger), and by never idling when a ready job fits in
the gap. The initial upper bound is the better of the FCFS and SJF orders.

## Numerics and determinism

- All times are `int64_t`. Validation guarantees
  `max(arrival) + sum(all phases)` fits, and the switch cost is bounded so that
  switch overhead cannot overflow either; the remaining additions use checked
  arithmetic.
- CFS vruntime is fixed point (2^16 units per nice-0 tick), computed as
  `floor(ran * 2^26 / weight)` without overflow.
- Floating point appears only in EWMA predictions and in reported averages.
  The build uses `-ffp-contract=off` so that no compiler fuses
  `alpha * t + (1 - alpha) * tau` into an FMA; C, WebAssembly and the Python
  reference then agree bit for bit.
- Every tie-break ends in the pid, which is unique, so every order is total.
  Lottery uses a local SplitMix64 generator with rejection sampling instead of
  `rand()`.

## Verification strategy

The layers of testing are chosen so each catches what the others cannot:

| Layer | Catches |
|---|---|
| Golden files from the original program | any change to the four original policies' output |
| Hand-computed cases | misunderstanding of a rule (the expected answer is worked on paper) |
| Differential vs `tools/reference_sim.py` | event-ordering and state bugs in rare coincidences, across random options |
| Schedule properties | impossible schedules even where the reference could share a misconception |
| Optimality theorems | wrong comparisons or tie-breaks that still produce "valid" schedules |
| ASan + UBSan, fuzzing | memory errors, overflow, parser edge cases |
| Mutation checks | tests that cannot fail |

The reference simulator is deliberately different in structure: it steps one
tick at a time, and for preemptive-on-arrival policies it simply re-decides
every tick instead of reacting to events or deadlines. Equivalence of the two
is therefore a real check of the engine's event logic rather than a restatement
of it.

## WebAssembly

The browser build compiles the unchanged CLI with Emscripten. The page writes
the workload to Emscripten's in-memory filesystem and calls `main()` with
`--format=json`; the output is parsed and drawn with plain SVG. Because the
engine has no global state, `main()` can run any number of times in one page.
`make wasm-test` runs several option combinations through both builds and
requires identical output. The one portability issue found was stack size:
Emscripten's default 64 KB stack was too small for the CSV parser's line
buffers, fixed by shrinking a buffer and setting a 1 MB stack.

## Performance

`make experiments` benchmarks every policy from 10^3 to 10^6 processes, at 90%
load and overloaded (arrivals at twice capacity, so the ready queue grows to
about n/2). The overloaded runs expose any per-decision linear scan. The
results and fitted log-log slopes are in [results](results/README.md); the
profile before and after the priority fix is in
[results/perf](results/perf/profile_prio_p.md).
