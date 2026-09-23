/* The pluggable scheduling-policy interface.
 *
 * The engine (engine.h) owns the clock, the cores, the timeline, I/O and all
 * per-process accounting. A policy only answers "who runs next, and for how
 * long?".
 *
 * Call protocol, driven by the engine. At every instant `now` (view->now):
 *   1. on_arrival(p)  for each process arriving at now, in (arrival, pid) order
 *   2. on_ready(p)    for each process whose I/O completes at now, in pid order
 *                     (NULL: on_arrival is used)
 *   3. slice outcomes, in core order, for slices ending at now:
 *        on_complete(p, ran)  last CPU burst finished (optional hook)
 *        on_block(p, ran)     CPU burst finished, p starts I/O (optional hook)
 *        on_preempt(p, ran)   p still has CPU work in this burst; it is ready again
 *      `ran` is the number of ticks p ran in the slice (0 if it only switched in).
 *   4. if preempt_on_ready and steps 1-2 made anything ready, every process
 *      that is running mid-slice is interrupted with on_preempt(p, ran)
 *   5. next_slice(&s) is called once per free core (in core order) until it
 *      returns false. The chosen process leaves the ready set. It runs for
 *      min(s.length, remaining) ticks, ending early at s.deadline (absolute).
 *      Returning false while processes are ready leaves cores idle; only
 *      policies with work_conserving == false may do that.
 *
 * Context switches (--cs-cost) happen inside the engine. A switch-in is not
 * interruptible; when it ends, a preempt_on_ready policy gets the process
 * back via on_preempt(p, 0) if a ready event happened during the switch or
 * the slice deadline has passed.
 *
 * Adding a policy: create src/policies/<name>.c defining a `const Policy`,
 * then add one line to the table in src/policies/registry.c.
 */
#ifndef SCHED_POLICY_H
#define SCHED_POLICY_H

#include "sched/core.h"

#define SCHED_NO_DEADLINE INT64_MAX
#define SCHED_MLFQ_MAX_LEVELS 16

/* Read-only view of the simulation that the engine lends to a policy. */
typedef struct {
    const Process *procs; /* the workload, indexed 0..n-1 */
    size_t n;
    size_t cores;
    int64_t now;              /* current simulated time */
    const int64_t *remaining; /* remaining ticks of each process's current CPU burst */
    const int64_t *burst_len; /* true length of each process's current CPU burst */
    const double *predicted;  /* EWMA prediction of the current CPU burst; NULL = oracle */
} SchedView;

typedef struct {
    int64_t quantum;                               /* RR, lottery, stride (> 0) */
    int64_t aging;                                 /* priority aging period K; 0 = off */
    size_t mlfq_levels;                            /* 1..SCHED_MLFQ_MAX_LEVELS */
    int64_t mlfq_quanta[SCHED_MLFQ_MAX_LEVELS];    /* time slice per level */
    int64_t mlfq_allotment[SCHED_MLFQ_MAX_LEVELS]; /* time allotment per level */
    int64_t mlfq_boost;                            /* boost period S; 0 = off */
    uint64_t seed;                                 /* lottery PRNG seed */
    int64_t cfs_latency;                           /* CFS target latency (ticks) */
    int64_t cfs_min_gran;                          /* CFS minimum granularity (ticks) */
} PolicyParams;

/* quantum 2, no aging, MLFQ 3 levels with quanta = allotments = 2,4,8 and no
 * boost, seed 1, CFS latency 24 / min granularity 3. */
void policy_params_default(PolicyParams *p);

typedef struct {
    size_t proc;      /* index into SchedView.procs */
    int64_t length;   /* requested ticks (> 0); the engine clamps it to remaining */
    int64_t deadline; /* absolute time the slice must end by; SCHED_NO_DEADLINE if none */
} Slice;

typedef struct Policy {
    const char *name;      /* CLI name, e.g. "fcfs" */
    const char *label;     /* short label for CSV / averages, e.g. "FCFS" */
    const char *heading;   /* text-mode heading, e.g. "FCFS (FIFO) Scheduling" */
    bool preemptive;       /* may take the CPU away from a process mid-burst */
    bool preempt_on_ready; /* engine interrupts running slices at arrivals / I/O completions */
    bool uses_quantum;     /* label/heading get a "(q=Q)" suffix */
    bool uses_estimates;   /* decisions depend on burst lengths (oracle or --predict) */
    bool work_conserving;  /* never idles a core while a process is ready */
    bool offline;          /* needs the whole workload up front (a baseline, not a
                              real scheduler); excluded from --algo=all */

    /* Returns NULL and sets err on failure (out of memory or unsupported input). */
    void *(*create)(const SchedView *view, const PolicyParams *params, SchedError *err);
    void (*destroy)(void *self);
    void (*on_arrival)(void *self, size_t proc);
    void (*on_ready)(void *self, size_t proc); /* optional */
    bool (*next_slice)(void *self, Slice *out);
    void (*on_preempt)(void *self, size_t proc, int64_t ran);  /* NULL: never preempted */
    void (*on_block)(void *self, size_t proc, int64_t ran);    /* optional */
    void (*on_complete)(void *self, size_t proc, int64_t ran); /* optional */
} Policy;

/* ---- helpers for policies that rank by (estimated) burst length ---- */

/* Expected length of the current CPU burst: exact under the oracle. */
static inline double sched_expected_burst(const SchedView *v, size_t p) {
    return v->predicted ? v->predicted[p] : (double)v->burst_len[p];
}

/* -1/0/1 comparing the expected current CPU burst of a and b. Exact integer
 * comparison under the oracle, EWMA predictions otherwise. */
static inline int sched_cmp_expected_burst(const SchedView *v, size_t a, size_t b) {
    if (!v->predicted)
        return (v->burst_len[a] > v->burst_len[b]) - (v->burst_len[a] < v->burst_len[b]);
    return (v->predicted[a] > v->predicted[b]) - (v->predicted[a] < v->predicted[b]);
}

/* -1/0/1 comparing expected remaining time of the current CPU burst. Under
 * EWMA this is max(prediction - ticks already run, 0). */
static inline int sched_cmp_expected_remaining(const SchedView *v, size_t a, size_t b) {
    if (!v->predicted)
        return (v->remaining[a] > v->remaining[b]) - (v->remaining[a] < v->remaining[b]);
    double ra = v->predicted[a] - (double)(v->burst_len[a] - v->remaining[a]);
    double rb = v->predicted[b] - (double)(v->burst_len[b] - v->remaining[b]);
    if (ra < 0) ra = 0;
    if (rb < 0) rb = 0;
    return (ra > rb) - (ra < rb);
}

/* ---- registry of built-in policies, in canonical output order ---- */
#define SCHED_MAX_POLICIES 64
size_t policy_count(void);
const Policy *policy_at(size_t index);
const Policy *policy_find(const char *name); /* NULL if unknown */

#endif
