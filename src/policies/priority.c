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
 * strictly better one.
 *
 * Data structure (O(log n) expected per operation). While a process waits,
 * waited(now) = c + now for a constant c = waited - enqueue time. Writing
 * c = q*K + r with 0 <= r < K gives
 *
 *     effective(now) = (priority - q) - floor(now / K) - [r >= K - now mod K]
 *
 * The middle term is shared by everyone, so the order depends on the fixed
 * A = priority - q and on whether r is above a moving threshold. Ready
 * processes live in a treap ordered by (r, pid) whose nodes also track the
 * subtree's best (A, arrival, pid); a decision takes the best of the r < T
 * range and of the r >= T range (which counts one level better). The next
 * aging step of any ready process comes from the largest r on either side of
 * the threshold. Without aging, r = 0 and A = priority for everyone.
 */
#include <stdlib.h>

#include "sched/checked.h"
#include "sched/policy.h"

#define NIL SIZE_MAX

typedef struct {
    const SchedView *view;
    bool preemptive;
    int64_t aging;
    int64_t *waited;      /* ready time accumulated in the current burst */
    int64_t *ready_since; /* when the process last entered the ready set */
    /* treap over ready processes, node = process index */
    size_t root;
    size_t *left, *right, *best;
    uint64_t *heap_prio;
    int64_t *r;    /* c mod K */
    int64_t *base; /* A = priority - floor(c / K), saturated */
} Prio;

static uint64_t mix(uint64_t z) { /* SplitMix64 finaliser: deterministic treap priorities */
    z += UINT64_C(0x9E3779B97F4A7C15);
    z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
    return z ^ (z >> 31);
}

static void prio_destroy(void *self) {
    Prio *s = self;
    free(s->waited);
    free(s->ready_since);
    free(s->left);
    free(s->right);
    free(s->best);
    free(s->heap_prio);
    free(s->r);
    free(s->base);
    free(s);
}

static void *create(const SchedView *view, const PolicyParams *params, SchedError *err,
                    bool preemptive) {
    size_t n = view->n;
    Prio *s = calloc(1, sizeof *s);
    if (s) {
        s->waited = calloc(n, sizeof *s->waited);
        s->ready_since = calloc(n, sizeof *s->ready_since);
        s->left = malloc(n * sizeof *s->left);
        s->right = malloc(n * sizeof *s->right);
        s->best = malloc(n * sizeof *s->best);
        s->heap_prio = malloc(n * sizeof *s->heap_prio);
        s->r = malloc(n * sizeof *s->r);
        s->base = malloc(n * sizeof *s->base);
    }
    if (!s || !s->waited || !s->ready_since || !s->left || !s->right || !s->best || !s->heap_prio ||
        !s->r || !s->base) {
        if (s) prio_destroy(s);
        sched_error_set(err, "out of memory");
        return NULL;
    }
    s->view = view;
    s->preemptive = preemptive;
    s->aging = params->aging;
    s->root = NIL;
    for (size_t i = 0; i < n; i++) s->heap_prio[i] = mix(i);
    return s;
}

static void *prio_create(const SchedView *view, const PolicyParams *params, SchedError *err) {
    return create(view, params, err, false);
}

static void *prio_p_create(const SchedView *view, const PolicyParams *params, SchedError *err) {
    return create(view, params, err, true);
}

/* ---- ordering ---- */

static int64_t sat_sub(int64_t a, int64_t b) {
    int64_t out;
    if (ck_sub_i64(a, b, &out)) return out;
    return b < 0 ? INT64_MAX : INT64_MIN;
}

/* (base - bump, arrival, pid) of x is smaller than that of y */
static bool key_less(const Prio *s, size_t x, int bump_x, size_t y, int bump_y) {
    int64_t ex = sat_sub(s->base[x], bump_x), ey = sat_sub(s->base[y], bump_y);
    if (ex != ey) return ex < ey;
    const Process *p = s->view->procs;
    if (p[x].arrival != p[y].arrival) return p[x].arrival < p[y].arrival;
    return p[x].pid < p[y].pid;
}

static size_t better_of(const Prio *s, size_t x, size_t y) {
    if (x == NIL) return y;
    if (y == NIL) return x;
    return key_less(s, y, 0, x, 0) ? y : x;
}

/* treap order: (r, pid) */
static bool node_before(const Prio *s, size_t x, size_t y) {
    if (s->r[x] != s->r[y]) return s->r[x] < s->r[y];
    return s->view->procs[x].pid < s->view->procs[y].pid;
}

/* ---- treap ---- */

static void pull(Prio *s, size_t t) {
    size_t b = t;
    if (s->left[t] != NIL) b = better_of(s, b, s->best[s->left[t]]);
    if (s->right[t] != NIL) b = better_of(s, b, s->best[s->right[t]]);
    s->best[t] = b;
}

/* Splits t into nodes ordered before x (*l) and the rest (*r). */
static void split(Prio *s, size_t t, size_t x, size_t *l, size_t *r) {
    if (t == NIL) {
        *l = *r = NIL;
        return;
    }
    if (node_before(s, t, x)) {
        split(s, s->right[t], x, &s->right[t], r);
        *l = t;
    } else {
        split(s, s->left[t], x, l, &s->left[t]);
        *r = t;
    }
    pull(s, t);
}

static size_t insert(Prio *s, size_t t, size_t x) {
    if (t == NIL) return x;
    if (s->heap_prio[x] > s->heap_prio[t]) {
        split(s, t, x, &s->left[x], &s->right[x]);
        pull(s, x);
        return x;
    }
    if (node_before(s, x, t)) {
        s->left[t] = insert(s, s->left[t], x);
    } else {
        s->right[t] = insert(s, s->right[t], x);
    }
    pull(s, t);
    return t;
}

static size_t merge(Prio *s, size_t a, size_t b) {
    if (a == NIL) return b;
    if (b == NIL) return a;
    if (s->heap_prio[a] > s->heap_prio[b]) {
        s->right[a] = merge(s, s->right[a], b);
        pull(s, a);
        return a;
    }
    s->left[b] = merge(s, a, s->left[b]);
    pull(s, b);
    return b;
}

static size_t erase(Prio *s, size_t t, size_t x) {
    if (t == x) return merge(s, s->left[t], s->right[t]);
    if (node_before(s, x, t)) {
        s->left[t] = erase(s, s->left[t], x);
    } else {
        s->right[t] = erase(s, s->right[t], x);
    }
    pull(s, t);
    return t;
}

/* Best (A, arrival, pid) among nodes with r < limit (below) or r >= limit. */
static size_t best_below(const Prio *s, int64_t limit) {
    size_t res = NIL;
    for (size_t t = s->root; t != NIL;) {
        if (s->r[t] < limit) {
            res = better_of(s, res, t);
            if (s->left[t] != NIL) res = better_of(s, res, s->best[s->left[t]]);
            t = s->right[t];
        } else {
            t = s->left[t];
        }
    }
    return res;
}

static size_t best_at_or_above(const Prio *s, int64_t limit) {
    size_t res = NIL;
    for (size_t t = s->root; t != NIL;) {
        if (s->r[t] >= limit) {
            res = better_of(s, res, t);
            if (s->right[t] != NIL) res = better_of(s, res, s->best[s->right[t]]);
            t = s->left[t];
        } else {
            t = s->right[t];
        }
    }
    return res;
}

/* Largest r below limit, or -1. */
static int64_t max_r_below(const Prio *s, int64_t limit) {
    int64_t ans = -1;
    for (size_t t = s->root; t != NIL;) {
        if (s->r[t] < limit) {
            ans = s->r[t];
            t = s->right[t];
        } else {
            t = s->left[t];
        }
    }
    return ans;
}

static int64_t max_r(const Prio *s) {
    size_t t = s->root;
    while (s->right[t] != NIL) t = s->right[t];
    return s->r[t];
}

/* ---- policy hooks ---- */

static void enqueue(Prio *s, size_t p) {
    int64_t now = s->view->now;
    s->ready_since[p] = now;
    int64_t base = s->view->procs[p].priority;
    if (s->aging > 0) {
        int64_t c = s->waited[p] - now, k = s->aging;
        int64_t q = c / k;
        if (c % k < 0) q--; /* floor division */
        s->r[p] = c - q * k;
        s->base[p] = sat_sub(base, q);
    } else {
        s->r[p] = 0;
        s->base[p] = base;
    }
    s->left[p] = s->right[p] = NIL;
    s->best[p] = p;
    s->root = insert(s, s->root, p);
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
    if (s->root == NIL) return false;
    int64_t now = s->view->now, k = s->aging;
    size_t p;
    int64_t limit = 0;
    if (k > 0) {
        limit = k - now % k; /* r >= limit: one aging level more than the rest */
        size_t low = best_below(s, limit), high = best_at_or_above(s, limit);
        p = low == NIL ? high : high == NIL ? low : key_less(s, high, 1, low, 0) ? high : low;
    } else {
        p = s->best[s->root];
    }
    s->root = erase(s, s->root, p);
    s->waited[p] += now - s->ready_since[p];
    out->proc = p;
    out->length = s->view->remaining[p];
    out->deadline = SCHED_NO_DEADLINE;
    if (s->preemptive && k > 0 && s->root != NIL) {
        /* Next instant a queued process gains a level: the one whose
         * (r + now) mod K is largest. */
        int64_t r_low = max_r_below(s, limit), step;
        if (r_low >= 0) {
            step = limit - r_low; /* K - (r + now mod K) */
        } else {
            int64_t r_high = max_r(s); /* >= limit */
            step = k - (r_high - limit);
        }
        if (!ck_add_i64(now, step, &out->deadline)) out->deadline = SCHED_NO_DEADLINE;
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
