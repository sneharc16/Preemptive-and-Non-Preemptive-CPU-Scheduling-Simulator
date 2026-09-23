/* Round Robin with a fixed quantum. New arrivals and preempted processes
 * share one FIFO; because the engine admits arrivals before reporting the
 * preemption, a process arriving during (or exactly at the end of) a slice is
 * queued ahead of the process that was just preempted. */
#include <stdlib.h>

#include "sched/policy.h"
#include "util/queue.h"

typedef struct {
    Queue ready;
    int64_t quantum;
} Rr;

static void *rr_create(const SchedView *view, const PolicyParams *params, SchedError *err) {
    Rr *s = malloc(sizeof *s);
    if (!s || !queue_init(&s->ready, view->n)) {
        free(s);
        sched_error_set(err, "out of memory");
        return NULL;
    }
    s->quantum = params->quantum;
    return s;
}

static void rr_destroy(void *self) {
    Rr *s = self;
    queue_free(&s->ready);
    free(s);
}

static void rr_enqueue(void *self, size_t proc) { queue_push(&((Rr *)self)->ready, proc); }

static void rr_requeue(void *self, size_t proc, int64_t ran) {
    (void)ran;
    rr_enqueue(self, proc);
}

static bool rr_next_slice(void *self, Slice *out) {
    Rr *s = self;
    if (queue_empty(&s->ready)) return false;
    out->proc = queue_pop(&s->ready);
    out->length = s->quantum;
    out->deadline = SCHED_NO_DEADLINE;
    return true;
}

const Policy POLICY_RR = {
    .name = "rr",
    .label = "RoundRobin",
    .heading = "Round Robin Scheduling",
    .preemptive = true,
    .preempt_on_ready = false,
    .uses_quantum = true,
    .uses_estimates = false,
    .work_conserving = true,
    .create = rr_create,
    .destroy = rr_destroy,
    .on_arrival = rr_enqueue,
    .next_slice = rr_next_slice,
    .on_preempt = rr_requeue,
};
