/* Shortest Remaining Time First (preemptive SJF): picks the ready process
 * with the smallest (remaining, arrival, pid). The engine ends every slice
 * at the next arrival so the choice is re-evaluated whenever the ready set
 * grows. The running process is outside the heap, so the remaining time of
 * every heap entry is constant while it sits there. */
#include <stdlib.h>

#include "sched/policy.h"
#include "util/heap.h"

typedef struct {
    const SchedView *view;
    Heap ready;
} Srtf;

static bool srtf_less(const void *ctx, size_t a, size_t b) {
    const SchedView *v = ctx;
    if (v->remaining[a] != v->remaining[b]) return v->remaining[a] < v->remaining[b];
    if (v->procs[a].arrival != v->procs[b].arrival)
        return v->procs[a].arrival < v->procs[b].arrival;
    return v->procs[a].pid < v->procs[b].pid;
}

static void *srtf_create(const SchedView *view, const PolicyParams *params) {
    (void)params;
    Srtf *s = malloc(sizeof *s);
    if (!s) return NULL;
    s->view = view;
    if (!heap_init(&s->ready, view->n, srtf_less, view)) {
        free(s);
        return NULL;
    }
    return s;
}

static void srtf_destroy(void *self) {
    Srtf *s = self;
    heap_free(&s->ready);
    free(s);
}

static void srtf_push(void *self, size_t proc) { heap_push(&((Srtf *)self)->ready, proc); }

static bool srtf_next_slice(void *self, Slice *out) {
    Srtf *s = self;
    if (heap_empty(&s->ready)) return false;
    out->proc = heap_pop(&s->ready);
    out->length = s->view->remaining[out->proc];
    return true;
}

const Policy POLICY_SRTF = {
    .name = "srtf",
    .label = "SRTF",
    .heading = "SRTF (Preemptive SJF) Scheduling",
    .preemptive = true,
    .preempt_on_arrival = true,
    .uses_quantum = false,
    .create = srtf_create,
    .destroy = srtf_destroy,
    .on_arrival = srtf_push,
    .next_slice = srtf_next_slice,
    .on_preempt = srtf_push,
    .on_complete = NULL,
};
