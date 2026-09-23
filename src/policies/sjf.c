/* Shortest Job First: non-preemptive, picks the ready process with the
 * smallest (current CPU burst, arrival, pid). The burst is exact under the
 * oracle and an EWMA prediction under --predict=ewma. */
#include <stdlib.h>

#include "sched/policy.h"
#include "util/heap.h"

typedef struct {
    const SchedView *view;
    Heap ready;
} Sjf;

static bool sjf_less(const void *ctx, size_t a, size_t b) {
    const SchedView *v = ctx;
    const Process *p = v->procs;
    int c = sched_cmp_expected_burst(v, a, b);
    if (c != 0) return c < 0;
    if (p[a].arrival != p[b].arrival) return p[a].arrival < p[b].arrival;
    return p[a].pid < p[b].pid;
}

static void *sjf_create(const SchedView *view, const PolicyParams *params, SchedError *err) {
    (void)params;
    Sjf *s = malloc(sizeof *s);
    if (!s || !heap_init(&s->ready, view->n, sjf_less, view)) {
        free(s);
        sched_error_set(err, "out of memory");
        return NULL;
    }
    s->view = view;
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
    out->deadline = SCHED_NO_DEADLINE;
    return true;
}

const Policy POLICY_SJF = {
    .name = "sjf",
    .label = "SJF",
    .heading = "SJF (Non-preemptive) Scheduling",
    .preemptive = false,
    .preempt_on_ready = false,
    .uses_quantum = false,
    .uses_estimates = true,
    .work_conserving = true,
    .create = sjf_create,
    .destroy = sjf_destroy,
    .on_arrival = sjf_on_arrival,
    .next_slice = sjf_next_slice,
    .on_preempt = NULL,
};
