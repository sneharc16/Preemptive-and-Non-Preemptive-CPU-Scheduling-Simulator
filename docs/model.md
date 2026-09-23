# Scheduling model

This is the precise model the simulator implements. The C engine
(`src/engine/engine.c`) and the independent tick-by-tick reference
(`tools/reference_sim.py`) both follow it, and the differential test
requires them to produce identical schedules.

## Time and events

Time is measured in integer ticks. A process arrives at `arrival` and is a
sequence of phases `cpu, io, cpu, ..., cpu` (a plain `burst` is a single CPU
phase). The engine jumps from event to event. At each instant `t`:

1. processes arriving at `t` join the ready set, in pid order;
2. processes whose I/O completes at `t` join the ready set, in pid order;
3. run slices that used their full length (or finished the CPU burst) end,
   in core order: the process completes, starts I/O, or rejoins the ready set;
4. context switches that finish at `t` start running their process;
5. interruptions, in core order: slices cut by a policy deadline and, for
   preemptive policies that react to new work (SRTF, prio-p, MLFQ), every
   running slice whenever the ready set gained a process at this instant or a
   deadline fell due;
6. free cores ask the policy for work.

The Round Robin rule "a process arriving during or exactly at the end of a
slice is queued before the preempted process" follows from steps 1 and 3.

## Machine model (all off by default)

**Context switches (`--cs-cost=C`).** When a core dispatches a process
different from the last one it ran, it first spends `C` ticks switching
(shown as a `cs` segment). The first dispatch on each core, and a process
continuing on the same core, are free. A switch cannot be interrupted, and the
switched-in process always runs at least one tick; if something happened
during the switch that calls for a new decision, it is taken after that
tick. (Re-deciding the moment a switch ends can livelock: with aging, the
waiting process can overtake the one being switched in on every switch.)
`waiting` includes switch-in time.

**I/O (`bursts` column).** A process that finishes a CPU phase blocks for the
following I/O phase and then rejoins the ready set. The I/O device has
unlimited capacity (I/O phases of different processes overlap freely).
Response time is arrival to first run, turnaround is arrival to final
completion, and waiting is turnaround minus total CPU minus total I/O.

**Burst prediction (`--predict=ewma --alpha=A --tau0=T`).** Real schedulers
cannot know burst lengths. With EWMA, SJF, SRTF and HRRN rank processes by a
prediction of the current CPU burst instead of its true length:
`tau_(n+1) = A * t_n + (1 - A) * tau_n`, starting at `T`, per process over its
own CPU bursts. SRTF uses `max(tau - ticks already run, 0)`. The simulator
reports prediction MAE and MAPE (schedule independent) and each policy's
regret: its mean turnaround increase over the same policy with exact bursts.
Floating-point code is built with `-ffp-contract=off` so predictions are
bit-identical across compilers and match the Python reference.

**Multiple cores (`--cores=N`).** One global ready set shared by all cores.
When several cores are free at once the policy is asked once per free core; a
process that last ran on one of the free cores goes back to that core, and
the rest fill free cores in index order. For SRTF, prio-p and MLFQ, all
running processes are re-chosen together whenever the ready set changes, so
the running set is always the best N by the policy's order.
Not modelled: per-core run queues, load balancing, cache affinity or warmth,
migration cost beyond the ordinary switch cost, SMT, and memory bandwidth.

## Policies

| `--algo` | Preemptive | Rule | Ties |
|----------|-----------|------|------|
| `fcfs`   | no  | Earliest arrival (FIFO of ready order) | pid |
| `sjf`    | no  | Shortest (expected) current CPU burst | arrival, pid |
| `srtf`   | yes | Shortest (expected) remaining time; re-decided when work arrives | arrival, pid |
| `rr`     | yes | FIFO with a fixed quantum (`--quantum`) | ready order |
| `prio`   | no  | Lowest effective priority | arrival, pid |
| `prio-p` | yes | Lowest effective priority; re-decided when work arrives or a waiting process ages | arrival, pid |
| `hrrn`   | no  | Highest response ratio `1 + W/S` (W = wait for this burst, S = expected burst), compared exactly | arrival, pid |
| `mlfq`   | yes | OSTEP chapter 8 rules (below) | queue order |
| `lottery`| yes | Random ticket draw each quantum, reproducible with `--seed` | - |
| `stride` | yes | Smallest pass, `pass += (2^20 / tickets) * ticks run` | pid |
| `cfs`    | yes | Smallest vruntime (CFS-lite, below) | pid |
| `opt-np` | no  | Offline exact optimum of mean turnaround (below) | first found |

**Aging (`--aging=K`, prio and prio-p).** Effective priority is
`priority - floor(W / K)`, where W is the time the process has spent in the
ready set during its current CPU burst. W does not grow while the process
runs or does I/O and resets when a new CPU burst starts. Without aging, a
steady stream of high-priority work starves a low-priority process
indefinitely; `tests/test_policies.py` demonstrates this.

**MLFQ.** Levels `--mlfq-levels` with time slices `--mlfq-quanta` (default
2,4,8) and allotments `--mlfq-allot` (default = the slices).
1. A higher queue always runs first; arrivals and I/O completions preempt
   lower queues.
2. Within a queue, jobs share the CPU round-robin with that level's slice.
3. New jobs enter the top queue.
4. Once a job has used its level's allotment, however many times it gave up
   the CPU, it moves down a level (the bottom level has no allotment), so a
   job cannot stay on top by doing I/O just before its slice expires.
5. Every `--mlfq-boost=S` ticks (at S, 2S, ...) all jobs return to the top
   queue with fresh allotments.

A job interrupted before its slice ends goes to the front of its queue and
keeps the rest of its slice; a job whose slice expires goes to the back; a
job returning from I/O keeps its level and used allotment and joins the back
with a fresh slice. At a boost the queues are concatenated top to bottom and
the running job's slice ends; it joins the back of the top queue.

**Lottery.** Each decision draws one ticket uniformly among the ready
processes' tickets (`tickets` column, default 100) with SplitMix64 and
rejection sampling, walking ready processes in pid order. The same `--seed`
gives the same schedule on every platform.

**Stride.** A new process starts at the current virtual time (the pass of the
last dispatched process); a process returning from I/O resumes at
`max(own pass, virtual time)`.

**CFS-lite.** A simplified model of the Linux Completely Fair Scheduler, not a
reimplementation. Weights come from the kernel's nice-to-weight table (nice 0
= 1024). vruntime advances by `ran * 1024 / weight` in fixed point (2^16 units
per nice-0 tick). The process with the smallest vruntime runs for
`max(latency * weight / total runnable weight, min_gran)` ticks
(`--cfs-latency`, default 24; `--cfs-min-gran`, default 3). `min_vruntime`
follows the leftmost queued process and never decreases; new processes start
at it, and a process waking from I/O gets `max(own, min_vruntime -
latency/2)`. Not modelled: wakeup preemption, `sched_nr_latency` period
stretching, START_DEBIT, group scheduling, per-CPU run queues and load
balancing, and the red-black tree itself (a binary heap gives the same order).

## Optimality

- **SRTF** minimises mean turnaround (and mean waiting) over all schedules on
  one CPU when preemption is free and burst lengths are known. Tests check it
  against an exhaustive search over every preemptive schedule.
- **SJF** is optimal among non-preemptive schedules only when all processes
  arrive together. With staggered arrivals it is a greedy heuristic: the
  problem, written 1|r_j|ΣC_j in scheduling notation, is strongly NP-hard.
  Smallest counterexample in the tests: P2 arrives at 5 with burst 9, P1 at 6
  with burst 2. SJF starts P2 at once (total turnaround 19); idling for one
  tick and running P1 first gives 14.
- **opt-np** computes the exact non-preemptive optimum by branch and bound
  over job orders, with the SRPT (preemptive) optimum of the remaining jobs as
  a lower bound, dominance pruning on (scheduled set, finish time, total), and
  never idling when a ready job fits in the gap. It supports up to 20
  processes on one core without I/O or switch cost and refuses anything else.
  `--gap` reports every policy's distance from it; preemptive policies can be
  below it.
