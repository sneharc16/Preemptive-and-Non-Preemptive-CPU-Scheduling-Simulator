/* Shortest Job First: non-preemptive, picks the ready process with the
 * smallest (burst, arrival, pid). */
#include <stdlib.h>

#include "sched/policy.h"
#include "util/heap.h"

typedef struct {
    const SchedView *view;
    Heap ready;
} Sjf;

static bool sjf_less(const void *ctx, size_t a, size_t b) {
    const Process *p = ((const SchedView *)ctx)->procs;
    if (p[a].burst != p[b].burst) return p[a].burst < p[b].burst;
    if (p[a].arrival != p[b].arrival) return p[a].arrival < p[b].arrival;
    return p[a].pid < p[b].pid;
}

static void *sjf_create(const SchedView *view, const PolicyParams *params) {
    (void)params;
    Sjf *s = malloc(sizeof *s);
    if (!s) return NULL;
    s->view = view;
    if (!heap_init(&s->ready, view->n, sjf_less, view)) {
        free(s);
        return NULL;
    }
    return s;
}

static void sjf_destroy(void *self) {
    Sjf *s = self;
    heap_free(&s->ready);
    free(s);
}

static void sjf_on_arrival(void *self, size_t proc) { heap_push(&((Sjf *)self)->ready, proc); }

static bool sjf_next_slice(void *self, Slice *out) {
    Sjf *s = self;
    if (heap_empty(&s->ready)) return false;
    out->proc = heap_pop(&s->ready);
    out->length = s->view->remaining[out->proc];
    return true;
}

const Policy POLICY_SJF = {
    .name = "sjf",
    .label = "SJF",
    .heading = "SJF (Non-preemptive) Scheduling",
    .preemptive = false,
    .preempt_on_arrival = false,
    .uses_quantum = false,
    .create = sjf_create,
    .destroy = sjf_destroy,
    .on_arrival = sjf_on_arrival,
    .next_slice = sjf_next_slice,
    .on_preempt = NULL,
    .on_complete = NULL,
};
