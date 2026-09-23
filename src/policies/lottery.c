/* Lottery scheduling (Waldspurger & Weihl, 1994): proportional share.
 *
 * Each time a CPU is free, one ticket is drawn uniformly from the tickets of
 * all ready processes (optional `tickets` column, default 100) and the owner
 * runs for one quantum. A draw happens at every decision, even with a single
 * ready process, so the random sequence depends only on the number of
 * decisions.
 *
 * Reproducibility: the generator is SplitMix64 seeded with --seed, and a
 * draw below T uses rejection sampling (discard x < 2^64 mod T, then x mod T),
 * so results are bit-identical on every platform. Ready processes are
 * ordered by pid for the ticket walk, implemented with a Fenwick tree over
 * pid ranks: O(log n) per draw.
 */
#include <stdlib.h>

#include "sched/policy.h"

typedef struct {
    const SchedView *view;
    int64_t quantum;
    uint64_t rng;
    size_t n;
    size_t *rank;    /* process index -> rank by pid */
    size_t *by_rank; /* rank -> process index */
    int64_t *tree;   /* Fenwick tree of ready tickets over ranks (1-based) */
    int64_t total;   /* tickets of all ready processes */
    size_t log2n;    /* highest power of two <= n */
} Lottery;

static uint64_t splitmix64(uint64_t *state) {
    uint64_t z = (*state += UINT64_C(0x9E3779B97F4A7C15));
    z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
    return z ^ (z >> 31);
}

/* Uniform integer in [0, bound), bound > 0. */
static uint64_t draw_below(uint64_t *state, uint64_t bound) {
    uint64_t threshold = (0 - bound) % bound; /* 2^64 mod bound */
    for (;;) {
        uint64_t x = splitmix64(state);
        if (x >= threshold) return x % bound;
    }
}

static void fenwick_add(Lottery *s, size_t rank, int64_t delta) {
    for (size_t i = rank + 1; i <= s->n; i += i & (0 - i)) s->tree[i] += delta;
}

/* Smallest rank whose prefix sum of tickets exceeds r. */
static size_t fenwick_find(const Lottery *s, int64_t r) {
    size_t pos = 0;
    for (size_t step = s->log2n; step > 0; step >>= 1) {
        if (pos + step <= s->n && s->tree[pos + step] <= r) {
            pos += step;
            r -= s->tree[pos];
        }
    }
    return pos; /* 0-based rank */
}

typedef struct {
    int64_t pid;
    size_t index;
} PidKey;

static int cmp_pid(const void *a, const void *b) {
    int64_t x = ((const PidKey *)a)->pid, y = ((const PidKey *)b)->pid;
    return (x > y) - (x < y);
}

static void lottery_destroy(void *self) {
    Lottery *s = self;
    free(s->rank);
    free(s->by_rank);
    free(s->tree);
    free(s);
}

static void *lottery_create(const SchedView *view, const PolicyParams *params, SchedError *err) {
    Lottery *s = calloc(1, sizeof *s);
    size_t n = view->n;
    if (s) {
        s->rank = malloc(n * sizeof *s->rank);
        s->by_rank = malloc(n * sizeof *s->by_rank);
        s->tree = calloc(n + 1, sizeof *s->tree);
    }
    if (!s || !s->rank || !s->by_rank || !s->tree) {
        if (s) {
            free(s->rank);
            free(s->by_rank);
            free(s->tree);
        }
        free(s);
        sched_error_set(err, "out of memory");
        return NULL;
    }
    s->view = view;
    s->n = n;
    s->quantum = params->quantum;
    s->rng = params->seed;
    PidKey *keys = malloc(n * sizeof *keys);
    if (!keys) {
        lottery_destroy(s);
        sched_error_set(err, "out of memory");
        return NULL;
    }
    for (size_t i = 0; i < n; i++) keys[i] = (PidKey){view->procs[i].pid, i};
    qsort(keys, n, sizeof *keys, cmp_pid); /* pids are unique: a total order */
    for (size_t r = 0; r < n; r++) {
        s->by_rank[r] = keys[r].index;
        s->rank[keys[r].index] = r;
    }
    free(keys);
    for (s->log2n = 1; s->log2n * 2 <= n; s->log2n *= 2) {
    }
    return s;
}

static void lottery_enqueue(void *self, size_t p) {
    Lottery *s = self;
    int64_t t = s->view->procs[p].tickets;
    fenwick_add(s, s->rank[p], t);
    s->total += t;
}

static void lottery_requeue(void *self, size_t p, int64_t ran) {
    (void)ran;
    lottery_enqueue(self, p);
}

static bool lottery_next_slice(void *self, Slice *out) {
    Lottery *s = self;
    if (s->total == 0) return false;
    int64_t winner = (int64_t)draw_below(&s->rng, (uint64_t)s->total);
    size_t p = s->by_rank[fenwick_find(s, winner)];
    int64_t t = s->view->procs[p].tickets;
    fenwick_add(s, s->rank[p], -t);
    s->total -= t;
    out->proc = p;
    out->length = s->quantum;
    out->deadline = SCHED_NO_DEADLINE;
    return true;
}

const Policy POLICY_LOTTERY = {
    .name = "lottery",
    .label = "Lottery",
    .heading = "Lottery Scheduling",
    .preemptive = true,
    .preempt_on_ready = false,
    .uses_quantum = true,
    .uses_estimates = false,
    .work_conserving = true,
    .create = lottery_create,
    .destroy = lottery_destroy,
    .on_arrival = lottery_enqueue,
    .next_slice = lottery_next_slice,
    .on_preempt = lottery_requeue,
};
