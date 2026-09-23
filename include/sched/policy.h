/* The pluggable scheduling-policy interface.
 *
 * The engine (engine.h) owns the clock, the timeline and all per-process
 * accounting. A policy only answers "who runs next, and for how long?".
 *
 * Call protocol, driven by the engine:
 *   create(view, params)
 *   on_arrival(p)    for every process whose arrival <= now, in (arrival, pid)
 *                    order, before the first next_slice at that time
 *   next_slice(&s)   returns false iff no process is ready; otherwise the
 *                    chosen process leaves the policy's ready set and runs for
 *                    min(s.length, remaining) ticks, further capped at the next
 *                    arrival when preempt_on_arrival is set
 *   -- after each slice the engine first admits every arrival <= now
 *      (on_arrival), THEN reports the slice outcome: --
 *   on_preempt(p)    the slice ended with work left; p is ready again
 *   on_complete(p)   p finished (optional hook, may be NULL)
 *   destroy(state)
 *
 * Adding a policy: create src/policies/<name>.c defining a `const Policy`,
 * then add one line to the table in src/policies/registry.c.
 */
#ifndef SCHED_POLICY_H
#define SCHED_POLICY_H

#include "sched/core.h"

/* Read-only view of the simulation that the engine lends to a policy. */
typedef struct {
    const Process *procs;     /* the workload, indexed 0..n-1 */
    const int64_t *remaining; /* remaining CPU time per process, kept by the engine */
    size_t n;
} SchedView;

typedef struct {
    int64_t quantum; /* time slice for quantum-based policies (> 0) */
} PolicyParams;

typedef struct {
    size_t proc;    /* index into SchedView.procs */
    int64_t length; /* requested ticks (> 0); the engine clamps it */
} Slice;

typedef struct Policy {
    const char *name;    /* CLI name, e.g. "fcfs" */
    const char *label;   /* short label for CSV / averages, e.g. "FCFS" */
    const char *heading; /* text-mode heading, e.g. "FCFS (FIFO) Scheduling" */
    bool preemptive;
    bool preempt_on_arrival; /* engine ends each slice at the next arrival */
    bool uses_quantum;       /* label/heading get a "(q=Q)" suffix */

    void *(*create)(const SchedView *view, const PolicyParams *params); /* NULL on OOM */
    void (*destroy)(void *self);
    void (*on_arrival)(void *self, size_t proc);
    bool (*next_slice)(void *self, Slice *out);
    void (*on_preempt)(void *self, size_t proc);  /* NULL for run-to-completion policies */
    void (*on_complete)(void *self, size_t proc); /* optional */
} Policy;

/* Registry of all built-in policies, in canonical output order. */
#define SCHED_MAX_POLICIES 64
size_t policy_count(void);
const Policy *policy_at(size_t index);
const Policy *policy_find(const char *name); /* NULL if unknown */

#endif
