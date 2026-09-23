/* Priority scheduling, non-preemptive (prio) and preemptive (prio-p).
 *
 * Lower `priority` values run first. With --aging=K a waiting process's
 * effective priority improves by 1 for every K ticks it has spent in the
 * ready queue during its current CPU burst:
 *
 *     effective = priority - floor(waited / K)
 *
 * Waiting time accumulates only while ready (not while running or doing I/O)
 * and resets when a new CPU burst starts. Ties are broken by (arrival, pid).
 *
 * prio-p re-evaluates whenever a process becomes ready and, with aging, at
 * the next instant any ready process's effective priority changes; the
 * running process keeps its key while it runs, so it is only displaced by a
 * strictly better one. Both variants scan the ready list: O(n) per decision.
 */
#include <stdlib.h>

#include "sched/policy.h"

typedef struct {
    const SchedView *view;
    bool preemptive;
    int64_t aging;
    size_t *ready; /* unordered */
    size_t nready;
    int64_t *waited;      /* ready time accumulated in the current burst */
    int64_t *ready_since; /* when the process last entered the ready list */
} Prio;

static void *create(const SchedView *view, const PolicyParams *params, SchedError *err,
                    bool preemptive) {
    Prio *s = calloc(1, sizeof *s);
    if (s) {
        s->ready = malloc(view->n * sizeof *s->ready);
        s->waited = calloc(view->n, sizeof *s->waited);
        s->ready_since = calloc(view->n, sizeof *s->ready_since);
    }
    if (!s || !s->ready || !s->waited || !s->ready_since) {
        if (s) {
            free(s->ready);
            free(s->waited);
            free(s->ready_since);
        }
        free(s);
        sched_error_set(err, "out of memory");
        return NULL;
    }
    s->view = view;
    s->preemptive = preemptive;
    s->aging = params->aging;
    return s;
}

static void *prio_create(const SchedView *view, const PolicyParams *params, SchedError *err) {
    return create(view, params, err, false);
}

static void *prio_p_create(const SchedView *view, const PolicyParams *params, SchedError *err) {
    return create(view, params, err, true);
}

static void prio_destroy(void *self) {
    Prio *s = self;
    free(s->ready);
    free(s->waited);
    free(s->ready_since);
    free(s);
}

/* Total ready time of a queued process in its current burst, as of now. */
static int64_t waited_now(const Prio *s, size_t p) {
    return s->waited[p] + (s->view->now - s->ready_since[p]);
}

static int64_t effective(const Prio *s, size_t p) {
    int64_t base = s->view->procs[p].priority;
    if (s->aging <= 0) return base;
    int64_t bonus = waited_now(s, p) / s->aging;
    return base < INT64_MIN + bonus ? INT64_MIN : base - bonus; /* saturate */
}

static bool better(const Prio *s, size_t a, size_t b) {
    int64_t ea = effective(s, a), eb = effective(s, b);
    if (ea != eb) return ea < eb;
    const Process *p = s->view->procs;
    if (p[a].arrival != p[b].arrival) return p[a].arrival < p[b].arrival;
    return p[a].pid < p[b].pid;
}

static void enqueue(Prio *s, size_t p) {
    s->ready_since[p] = s->view->now;
    s->ready[s->nready++] = p;
}

static void prio_new_burst(void *self, size_t p) {
    Prio *s = self;
    s->waited[p] = 0;
    enqueue(s, p);
}

static void prio_requeue(void *self, size_t p, int64_t ran) {
    (void)ran;
    enqueue(self, p);
}

static bool prio_next_slice(void *self, Slice *out) {
    Prio *s = self;
    if (s->nready == 0) return false;
    size_t best = 0;
    for (size_t i = 1; i < s->nready; i++) {
        if (better(s, s->ready[i], s->ready[best])) best = i;
    }
    size_t p = s->ready[best];
    s->ready[best] = s->ready[--s->nready];
    s->waited[p] = waited_now(s, p);
    out->proc = p;
    out->length = s->view->remaining[p];
    out->deadline = SCHED_NO_DEADLINE;
    if (s->preemptive && s->aging > 0) {
        /* Re-evaluate when the next queued process gains a priority level. */
        for (size_t i = 0; i < s->nready; i++) {
            int64_t step = s->aging - waited_now(s, s->ready[i]) % s->aging;
            if (step < out->deadline - s->view->now) out->deadline = s->view->now + step;
        }
    }
    return true;
}

const Policy POLICY_PRIO = {
    .name = "prio",
    .label = "Priority",
    .heading = "Priority (Non-preemptive) Scheduling",
    .preemptive = false,
    .preempt_on_ready = false,
    .uses_quantum = false,
    .uses_estimates = false,
    .work_conserving = true,
    .create = prio_create,
    .destroy = prio_destroy,
    .on_arrival = prio_new_burst,
    .on_ready = prio_new_burst,
    .next_slice = prio_next_slice,
    .on_preempt = NULL,
};

const Policy POLICY_PRIO_P = {
    .name = "prio-p",
    .label = "PriorityP",
    .heading = "Priority (Preemptive) Scheduling",
    .preemptive = true,
    .preempt_on_ready = true,
    .uses_quantum = false,
    .uses_estimates = false,
    .work_conserving = true,
    .create = prio_p_create,
    .destroy = prio_destroy,
    .on_arrival = prio_new_burst,
    .on_ready = prio_new_burst,
    .next_slice = prio_next_slice,
    .on_preempt = prio_requeue,
};
