/* Highest Response Ratio Next (non-preemptive).
 *
 * When the CPU is free, run the ready process with the largest
 *
 *     response ratio = (W + S) / S = 1 + W / S
 *
 * where W is how long it has waited in the ready queue for its current CPU
 * burst and S is that burst's (expected) length. Under the oracle the ratios
 * are compared exactly as fractions; under --predict=ewma S is a double.
 * Ties go to the earlier (arrival, pid). O(n) per decision.
 */
#include <stdlib.h>

#include "sched/policy.h"

typedef struct {
    const SchedView *view;
    size_t *ready;
    size_t nready;
    int64_t *ready_since;
} Hrrn;

/* Sign of a/b - c/d for a, c >= 0 and b, d > 0, without overflow. */
static int cmp_fraction(int64_t a, int64_t b, int64_t c, int64_t d) {
    int sign = 1;
    for (;;) {
        int64_t qa = a / b, qc = c / d;
        if (qa != qc) return qa < qc ? -sign : sign;
        int64_t ra = a % b, rc = c % d;
        if (ra == 0 || rc == 0) return sign * ((ra != 0) - (rc != 0));
        /* ra/b vs rc/d  has the opposite sign of  b/ra vs d/rc */
        a = b;
        b = ra;
        int64_t nc = d;
        d = rc;
        c = nc;
        sign = -sign;
    }
}

/* Sign of ratio(x) - ratio(y), i.e. of W_x/S_x - W_y/S_y. */
static int cmp_ratio(const Hrrn *s, size_t x, size_t y) {
    const SchedView *v = s->view;
    int64_t wx = v->now - s->ready_since[x], wy = v->now - s->ready_since[y];
    if (!v->predicted) return cmp_fraction(wx, v->burst_len[x], wy, v->burst_len[y]);
    double rx = (double)wx / v->predicted[x], ry = (double)wy / v->predicted[y];
    return (rx > ry) - (rx < ry);
}

static bool better(const Hrrn *s, size_t a, size_t b) {
    int c = cmp_ratio(s, a, b);
    if (c != 0) return c > 0;
    const Process *p = s->view->procs;
    if (p[a].arrival != p[b].arrival) return p[a].arrival < p[b].arrival;
    return p[a].pid < p[b].pid;
}

static void *hrrn_create(const SchedView *view, const PolicyParams *params, SchedError *err) {
    (void)params;
    Hrrn *s = calloc(1, sizeof *s);
    if (s) {
        s->ready = malloc(view->n * sizeof *s->ready);
        s->ready_since = malloc(view->n * sizeof *s->ready_since);
    }
    if (!s || !s->ready || !s->ready_since) {
        if (s) {
            free(s->ready);
            free(s->ready_since);
        }
        free(s);
        sched_error_set(err, "out of memory");
        return NULL;
    }
    s->view = view;
    return s;
}

static void hrrn_destroy(void *self) {
    Hrrn *s = self;
    free(s->ready);
    free(s->ready_since);
    free(s);
}

static void hrrn_enqueue(void *self, size_t p) {
    Hrrn *s = self;
    s->ready_since[p] = s->view->now;
    s->ready[s->nready++] = p;
}

static bool hrrn_next_slice(void *self, Slice *out) {
    Hrrn *s = self;
    if (s->nready == 0) return false;
    size_t best = 0;
    for (size_t i = 1; i < s->nready; i++) {
        if (better(s, s->ready[i], s->ready[best])) best = i;
    }
    out->proc = s->ready[best];
    s->ready[best] = s->ready[--s->nready];
    out->length = s->view->remaining[out->proc];
    out->deadline = SCHED_NO_DEADLINE;
    return true;
}

const Policy POLICY_HRRN = {
    .name = "hrrn",
    .label = "HRRN",
    .heading = "HRRN (Highest Response Ratio Next) Scheduling",
    .preemptive = false,
    .preempt_on_ready = false,
    .uses_quantum = false,
    .uses_estimates = true,
    .work_conserving = true,
    .create = hrrn_create,
    .destroy = hrrn_destroy,
    .on_arrival = hrrn_enqueue,
    .next_slice = hrrn_next_slice,
    .on_preempt = NULL,
};
