/* Shortest Remaining Time First (preemptive SJF): picks the ready process
 * with the smallest (remaining, arrival, pid), where remaining is the
 * (expected) remaining time of the current CPU burst. The engine interrupts
 * running slices whenever a process becomes ready, so the choice is
 * re-evaluated each time the ready set grows. The running process is outside
 * the heap, so every heap key is constant while it sits there. */
#include <stdlib.h>

#include "sched/policy.h"
#include "util/heap.h"

typedef struct {
    const SchedView *view;
    Heap ready;
} Srtf;

static bool srtf_less(const void *ctx, size_t a, size_t b) {
    const SchedView *v = ctx;
    int c = sched_cmp_expected_remaining(v, a, b);
    if (c != 0) return c < 0;
    if (v->procs[a].arrival != v->procs[b].arrival)
        return v->procs[a].arrival < v->procs[b].arrival;
    return v->procs[a].pid < v->procs[b].pid;
}

static void *srtf_create(const SchedView *view, const PolicyParams *params, SchedError *err) {
    (void)params;
    Srtf *s = malloc(sizeof *s);
    if (!s || !heap_init(&s->ready, view->n, srtf_less, view)) {
        free(s);
        sched_error_set(err, "out of memory");
        return NULL;
    }
    s->view = view;
    return s;
}

static void srtf_destroy(void *self) {
    Srtf *s = self;
    heap_free(&s->ready);
    free(s);
}

static void srtf_push(void *self, size_t proc) { heap_push(&((Srtf *)self)->ready, proc); }

static void srtf_requeue(void *self, size_t proc, int64_t ran) {
    (void)ran;
    srtf_push(self, proc);
}

static bool srtf_next_slice(void *self, Slice *out) {
    Srtf *s = self;
    if (heap_empty(&s->ready)) return false;
    out->proc = heap_pop(&s->ready);
    out->length = s->view->remaining[out->proc];
    out->deadline = SCHED_NO_DEADLINE;
    return true;
}

const Policy POLICY_SRTF = {
    .name = "srtf",
    .label = "SRTF",
    .heading = "SRTF (Preemptive SJF) Scheduling",
    .preemptive = true,
    .preempt_on_ready = true,
    .uses_quantum = false,
    .uses_estimates = true,
    .work_conserving = true,
    .create = srtf_create,
    .destroy = srtf_destroy,
    .on_arrival = srtf_push,
    .next_slice = srtf_next_slice,
    .on_preempt = srtf_requeue,
};
