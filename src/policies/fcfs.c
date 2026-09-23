/* First-Come First-Served: non-preemptive, ties broken by (arrival, pid).
 * The engine admits processes in (arrival, pid) order, so a FIFO of
 * admissions already encodes the tie-break. */
#include <stdlib.h>

#include "sched/policy.h"
#include "util/queue.h"

typedef struct {
    const SchedView *view;
    Queue ready;
} Fcfs;

static void *fcfs_create(const SchedView *view, const PolicyParams *params) {
    (void)params;
    Fcfs *s = malloc(sizeof *s);
    if (!s) return NULL;
    s->view = view;
    if (!queue_init(&s->ready, view->n)) {
        free(s);
        return NULL;
    }
    return s;
}

static void fcfs_destroy(void *self) {
    Fcfs *s = self;
    queue_free(&s->ready);
    free(s);
}

static void fcfs_on_arrival(void *self, size_t proc) { queue_push(&((Fcfs *)self)->ready, proc); }

static bool fcfs_next_slice(void *self, Slice *out) {
    Fcfs *s = self;
    if (queue_empty(&s->ready)) return false;
    out->proc = queue_pop(&s->ready);
    out->length = s->view->remaining[out->proc];
    return true;
}

const Policy POLICY_FCFS = {
    .name = "fcfs",
    .label = "FCFS",
    .heading = "FCFS (FIFO) Scheduling",
    .preemptive = false,
    .preempt_on_arrival = false,
    .uses_quantum = false,
    .create = fcfs_create,
    .destroy = fcfs_destroy,
    .on_arrival = fcfs_on_arrival,
    .next_slice = fcfs_next_slice,
    .on_preempt = NULL,
    .on_complete = NULL,
};
