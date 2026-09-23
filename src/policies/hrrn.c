/* Highest Response Ratio Next (non-preemptive).
 *
 * When the CPU is free, run the ready process with the largest
 *
 *     response ratio = (W + S) / S = 1 + W / S
 *
 * where W is how long it has waited in the ready queue for its current CPU
 * burst and S is that burst's (expected) length. Under the oracle the ratios
 * are compared exactly as fractions; under --predict=ewma S is a double.
 * Ties go to the earlier (arrival, pid).
 *
 * Data structure: ready processes are grouped by S. Within a group the
 * process that became ready first has the largest W, hence the best ratio,
 * so each group is a queue ordered by (ready time, arrival, pid) and a
 * decision compares only the heads of the non-empty groups:
 * O(distinct burst lengths among ready processes) per decision instead of
 * O(ready processes). Empty groups are recycled, so at most n exist at once
 * and all storage is allocated up front.
 */
#include <stdlib.h>
#include <string.h>

#include "sched/policy.h"

#define NIL SIZE_MAX

typedef struct {
    const SchedView *view;
    int64_t *ready_since;
    size_t *next, *prev; /* per-process links within its group */
    size_t *group_of;
    /* groups, one per distinct S among ready processes */
    size_t *free_ids; /* stack of unused group ids */
    size_t nfree;
    size_t *head, *tail;
    uint64_t *key;               /* bit pattern of S */
    size_t *slot_of;             /* hash slot holding each group */
    size_t *act_next, *act_prev; /* list of non-empty groups */
    size_t active;
    /* open-addressing map: key -> group + 1 (0 = empty slot) */
    size_t *slots;
    size_t mask;
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

/* Queue order within a group: (ready time, arrival, pid). */
static bool queued_before(const Hrrn *s, size_t a, size_t b) {
    if (s->ready_since[a] != s->ready_since[b]) return s->ready_since[a] < s->ready_since[b];
    const Process *p = s->view->procs;
    if (p[a].arrival != p[b].arrival) return p[a].arrival < p[b].arrival;
    return p[a].pid < p[b].pid;
}

static void hrrn_destroy(void *self) {
    Hrrn *s = self;
    free(s->ready_since);
    free(s->next);
    free(s->prev);
    free(s->group_of);
    free(s->head);
    free(s->tail);
    free(s->key);
    free(s->slot_of);
    free(s->free_ids);
    free(s->act_next);
    free(s->act_prev);
    free(s->slots);
    free(s);
}

static void *hrrn_create(const SchedView *view, const PolicyParams *params, SchedError *err) {
    (void)params;
    size_t n = view->n, nslots = 2;
    while (nslots < 2 * n) nslots *= 2;
    Hrrn *s = calloc(1, sizeof *s);
    if (s) {
        s->ready_since = malloc(n * sizeof *s->ready_since);
        s->next = malloc(n * sizeof *s->next);
        s->prev = malloc(n * sizeof *s->prev);
        s->group_of = malloc(n * sizeof *s->group_of);
        s->head = malloc(n * sizeof *s->head);
        s->tail = malloc(n * sizeof *s->tail);
        s->key = malloc(n * sizeof *s->key);
        s->slot_of = malloc(n * sizeof *s->slot_of);
        s->free_ids = malloc(n * sizeof *s->free_ids);
        s->act_next = malloc(n * sizeof *s->act_next);
        s->act_prev = malloc(n * sizeof *s->act_prev);
        s->slots = calloc(nslots, sizeof *s->slots);
    }
    if (!s || !s->ready_since || !s->next || !s->prev || !s->group_of || !s->head || !s->tail ||
        !s->key || !s->slot_of || !s->free_ids || !s->act_next || !s->act_prev || !s->slots) {
        if (s) hrrn_destroy(s);
        sched_error_set(err, "out of memory");
        return NULL;
    }
    s->view = view;
    s->mask = nslots - 1;
    s->active = NIL;
    for (size_t g = 0; g < n; g++) s->free_ids[g] = n - 1 - g;
    s->nfree = n;
    return s;
}

static uint64_t burst_key(const Hrrn *s, size_t p) {
    if (!s->view->predicted) return (uint64_t)s->view->burst_len[p];
    uint64_t bits;
    memcpy(&bits, &s->view->predicted[p], sizeof bits);
    return bits;
}

static uint64_t hash(uint64_t z) {
    z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
    return z ^ (z >> 31);
}

/* Group for key k, created if no ready process has that key. */
static size_t group_for(Hrrn *s, uint64_t k) {
    for (size_t i = hash(k) & s->mask;; i = (i + 1) & s->mask) {
        if (s->slots[i] == 0) {
            size_t g = s->free_ids[--s->nfree];
            s->slots[i] = g + 1;
            s->slot_of[g] = i;
            s->key[g] = k;
            s->head[g] = s->tail[g] = NIL;
            return g;
        }
        if (s->key[s->slots[i] - 1] == k) return s->slots[i] - 1;
    }
}

/* Removes an empty group from the map (linear probing, backward shift). */
static void release_group(Hrrn *s, size_t g) {
    size_t i = s->slot_of[g];
    for (size_t j = (i + 1) & s->mask; s->slots[j] != 0; j = (j + 1) & s->mask) {
        size_t home = hash(s->key[s->slots[j] - 1]) & s->mask;
        /* move slot j into the hole at i unless its home lies in (i, j] */
        bool home_between = i <= j ? (home > i && home <= j) : (home > i || home <= j);
        if (!home_between) {
            s->slots[i] = s->slots[j];
            s->slot_of[s->slots[i] - 1] = i;
            i = j;
        }
    }
    s->slots[i] = 0;
    s->free_ids[s->nfree++] = g;
}

static void hrrn_enqueue(void *self, size_t p) {
    Hrrn *s = self;
    s->ready_since[p] = s->view->now;
    size_t g = group_for(s, burst_key(s, p));
    s->group_of[p] = g;
    if (s->head[g] == NIL) { /* group becomes active */
        s->act_prev[g] = NIL;
        s->act_next[g] = s->active;
        if (s->active != NIL) s->act_prev[s->active] = g;
        s->active = g;
    }
    /* Insert from the tail: entries arrive in ready-time order, so this is
     * O(1) except among processes that became ready at the same instant. */
    size_t after = s->tail[g];
    while (after != NIL && queued_before(s, p, after)) after = s->prev[after];
    s->prev[p] = after;
    s->next[p] = after == NIL ? s->head[g] : s->next[after];
    if (after == NIL) {
        s->head[g] = p;
    } else {
        s->next[after] = p;
    }
    if (s->next[p] == NIL) {
        s->tail[g] = p;
    } else {
        s->prev[s->next[p]] = p;
    }
}

static bool hrrn_next_slice(void *self, Slice *out) {
    Hrrn *s = self;
    if (s->active == NIL) return false;
    size_t best = NIL;
    for (size_t g = s->active; g != NIL; g = s->act_next[g]) {
        if (best == NIL || better(s, s->head[g], best)) best = s->head[g];
    }
    size_t g = s->group_of[best];
    s->head[g] = s->next[best];
    if (s->head[g] == NIL) { /* group becomes inactive */
        s->tail[g] = NIL;
        if (s->act_prev[g] == NIL) {
            s->active = s->act_next[g];
        } else {
            s->act_next[s->act_prev[g]] = s->act_next[g];
        }
        if (s->act_next[g] != NIL) s->act_prev[s->act_next[g]] = s->act_prev[g];
        release_group(s, g);
    } else {
        s->prev[s->head[g]] = NIL;
    }
    out->proc = best;
    out->length = s->view->remaining[best];
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
