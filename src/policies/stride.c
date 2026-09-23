/* Stride scheduling (Waldspurger, 1995): deterministic proportional share.
 *
 * Each process has stride = 2^20 / tickets and a pass value. The ready
 * process with the smallest (pass, pid) runs for one quantum, after which
 * its pass advances by stride * ticks actually run, so partial slices are
 * charged fairly.
 *
 * Joining: a new process starts at the current virtual time (the pass of
 * the most recently dispatched process) so it cannot monopolise the CPU; a
 * process returning from I/O resumes at max(its pass, virtual time).
 */
#include <inttypes.h>
#include <stdlib.h>

#include "sched/policy.h"
#include "util/heap.h"

#define STRIDE1 ((int64_t)1 << 20)

typedef struct {
    const SchedView *view;
    int64_t quantum;
    int64_t *pass;
    int64_t vtime;
    Heap ready;
} Stride;

static int64_t stride_of(const Stride *s, size_t p) { return STRIDE1 / s->view->procs[p].tickets; }

static bool stride_less(const void *ctx, size_t a, size_t b) {
    const Stride *s = ctx;
    if (s->pass[a] != s->pass[b]) return s->pass[a] < s->pass[b];
    return s->view->procs[a].pid < s->view->procs[b].pid;
}

static void *stride_create(const SchedView *view, const PolicyParams *params, SchedError *err) {
    /* pass never exceeds STRIDE1 * (total CPU time) */
    int64_t total = 0;
    for (size_t i = 0; i < view->n; i++) {
        if (view->procs[i].burst > (INT64_MAX / STRIDE1) - total) {
            sched_error_set(err, "stride: total CPU time must be below %" PRId64 " ticks",
                            INT64_MAX / STRIDE1);
            return NULL;
        }
        total += view->procs[i].burst;
    }
    Stride *s = calloc(1, sizeof *s);
    if (s) s->pass = calloc(view->n, sizeof *s->pass);
    if (!s || !s->pass || !heap_init(&s->ready, view->n, stride_less, s)) {
        if (s) free(s->pass);
        free(s);
        sched_error_set(err, "out of memory");
        return NULL;
    }
    s->view = view;
    s->quantum = params->quantum;
    return s;
}

static void stride_destroy(void *self) {
    Stride *s = self;
    heap_free(&s->ready);
    free(s->pass);
    free(s);
}

static void stride_arrival(void *self, size_t p) {
    Stride *s = self;
    s->pass[p] = s->vtime;
    heap_push(&s->ready, p);
}

static void stride_ready(void *self, size_t p) {
    Stride *s = self;
    if (s->pass[p] < s->vtime) s->pass[p] = s->vtime;
    heap_push(&s->ready, p);
}

static void charge(Stride *s, size_t p, int64_t ran) { s->pass[p] += stride_of(s, p) * ran; }

static void stride_preempt(void *self, size_t p, int64_t ran) {
    Stride *s = self;
    charge(s, p, ran);
    heap_push(&s->ready, p);
}

static void stride_done(void *self, size_t p, int64_t ran) { charge(self, p, ran); }

static bool stride_next_slice(void *self, Slice *out) {
    Stride *s = self;
    if (heap_empty(&s->ready)) return false;
    out->proc = heap_pop(&s->ready);
    s->vtime = s->pass[out->proc];
    out->length = s->quantum;
    out->deadline = SCHED_NO_DEADLINE;
    return true;
}

const Policy POLICY_STRIDE = {
    .name = "stride",
    .label = "Stride",
    .heading = "Stride Scheduling",
    .preemptive = true,
    .preempt_on_ready = false,
    .uses_quantum = true,
    .uses_estimates = false,
    .work_conserving = true,
    .create = stride_create,
    .destroy = stride_destroy,
    .on_arrival = stride_arrival,
    .on_ready = stride_ready,
    .next_slice = stride_next_slice,
    .on_preempt = stride_preempt,
    .on_block = stride_done,
    .on_complete = stride_done,
};
