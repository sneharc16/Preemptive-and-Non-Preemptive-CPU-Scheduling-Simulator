/* opt-np: the exact minimum-mean-turnaround non-preemptive schedule.
 *
 * An offline baseline, not a real scheduler: it reads every arrival and
 * burst up front. Minimising total turnaround with release dates on one
 * machine without preemption (1|r_j|sum C_j) is strongly NP-hard, so this is
 * an exact branch-and-bound over job orders; a job starts at
 * max(previous completion, its arrival), which lets the optimum idle on
 * purpose while a short job is about to arrive.
 *
 * Pruning:
 *   - lower bound: current total + the SRPT (preemptive) optimum of the
 *     remaining jobs from the current time. SRPT is optimal when preemption
 *     is allowed, so no non-preemptive completion can beat it.
 *   - dominance: two partial schedules over the same set of jobs; one that
 *     finishes no later with no larger total makes the other redundant.
 *   - no pointless idling: never wait for job j if some ready job fits
 *     entirely in the gap before j arrives.
 * The initial upper bound is the better of the FCFS and SJF orders.
 *
 * Supported: at most 20 processes, one core, no I/O phases, no switch cost.
 * Among optimal schedules the one found first is kept; candidates are tried
 * in (completion, arrival, pid) order, so the result is deterministic.
 */
#include <stdlib.h>
#include <string.h>

#include "sched/policy.h"

#define OPT_NP_MAX 20
#define MEMO_MAX_NODES ((size_t)1 << 22) /* ~100 MB; beyond it the search just memoises less */

typedef struct {
    int64_t t;   /* completion time of the partial schedule */
    int64_t sum; /* total turnaround so far */
    int32_t next;
} MemoNode;

typedef struct {
    size_t n;
    int64_t r[OPT_NP_MAX], p[OPT_NP_MAX], pid[OPT_NP_MAX];
    int64_t best;
    int order[OPT_NP_MAX], best_order[OPT_NP_MAX];
    int32_t *memo_head; /* per subset of scheduled jobs */
    MemoNode *pool;
    size_t pool_len, pool_cap;
    bool memo_full;
} Search;

/* SRPT total turnaround of the jobs not in `mask`, starting at time t. */
static int64_t srpt_bound(const Search *s, uint32_t mask, int64_t t) {
    int64_t rem[OPT_NP_MAX], rel[OPT_NP_MAX];
    size_t k = 0;
    for (size_t j = 0; j < s->n; j++) {
        if (mask & (1u << j)) continue;
        rem[k] = s->p[j];
        rel[k] = s->r[j];
        k++;
    }
    int64_t total = 0;
    size_t left = k;
    while (left > 0) {
        /* shortest remaining among released; else jump to next release */
        size_t best = k;
        int64_t next_release = INT64_MAX;
        for (size_t i = 0; i < k; i++) {
            if (rem[i] == 0) continue;
            if (rel[i] <= t) {
                if (best == k || rem[i] < rem[best]) best = i;
            } else if (rel[i] < next_release) {
                next_release = rel[i];
            }
        }
        if (best == k) {
            t = next_release;
            continue;
        }
        int64_t run = rem[best];
        if (next_release != INT64_MAX && next_release - t < run) run = next_release - t;
        t += run;
        rem[best] -= run;
        if (rem[best] == 0) {
            total += t - rel[best];
            left--;
        }
    }
    return total;
}

/* Returns true if (t, sum) is dominated for `mask`; otherwise records it. */
static bool dominated(Search *s, uint32_t mask, int64_t t, int64_t sum) {
    for (int32_t i = s->memo_head[mask]; i >= 0; i = s->pool[i].next) {
        if (s->pool[i].t <= t && s->pool[i].sum <= sum) return true;
    }
    if (s->memo_full) return false;
    if (s->pool_len == s->pool_cap) {
        size_t ncap = s->pool_cap ? s->pool_cap * 2 : 1024;
        MemoNode *np = ncap <= MEMO_MAX_NODES ? realloc(s->pool, ncap * sizeof *np) : NULL;
        if (!np) {
            s->memo_full = true; /* keep searching, just without more memo entries */
            return false;
        }
        s->pool = np;
        s->pool_cap = ncap;
    }
    s->pool[s->pool_len] = (MemoNode){.t = t, .sum = sum, .next = s->memo_head[mask]};
    s->memo_head[mask] = (int32_t)s->pool_len++;
    return false;
}

static void search(Search *s, uint32_t mask, size_t depth, int64_t t, int64_t sum) {
    if (depth == s->n) {
        if (sum < s->best) {
            s->best = sum;
            memcpy(s->best_order, s->order, sizeof s->order);
        }
        return;
    }
    if (sum + srpt_bound(s, mask, t) >= s->best) return;
    if (dominated(s, mask, t, sum)) return;

    /* Candidates in (completion, arrival, pid) order. */
    int cand[OPT_NP_MAX];
    int64_t done[OPT_NP_MAX];
    size_t nc = 0;
    for (size_t j = 0; j < s->n; j++) {
        if (mask & (1u << j)) continue;
        int64_t start = t > s->r[j] ? t : s->r[j];
        /* Waiting for j is pointless if a ready job fits before it arrives. */
        bool gap_fillable = false;
        for (size_t k = 0; k < s->n && start > t; k++) {
            if (!(mask & (1u << k)) && k != j && s->r[k] <= t && s->p[k] <= start - t) {
                gap_fillable = true;
            }
        }
        if (gap_fillable) continue;
        size_t at = nc++;
        int64_t c = start + s->p[j];
        while (at > 0) {
            int prev = cand[at - 1];
            bool after =
                done[at - 1] < c ||
                (done[at - 1] == c &&
                 (s->r[prev] < s->r[j] || (s->r[prev] == s->r[j] && s->pid[prev] < s->pid[j])));
            if (after) break;
            cand[at] = cand[at - 1];
            done[at] = done[at - 1];
            at--;
        }
        cand[at] = (int)j;
        done[at] = c;
    }
    for (size_t i = 0; i < nc; i++) {
        int j = cand[i];
        s->order[depth] = j;
        search(s, mask | (1u << j), depth + 1, done[i], sum + done[i] - s->r[j]);
    }
}

/* Total turnaround of running jobs in `order`, each as early as possible. */
static int64_t order_total(const Search *s, const int *order) {
    int64_t t = 0, sum = 0;
    for (size_t i = 0; i < s->n; i++) {
        int j = order[i];
        t = (t > s->r[j] ? t : s->r[j]) + s->p[j];
        sum += t - s->r[j];
    }
    return sum;
}

/* Greedy non-preemptive order picking min(key) among released jobs. */
static void greedy(const Search *s, bool by_burst, int *order) {
    uint32_t mask = 0;
    int64_t t = 0;
    for (size_t i = 0; i < s->n; i++) {
        int best = -1;
        int64_t earliest = INT64_MAX;
        for (size_t j = 0; j < s->n; j++) {
            if (!(mask & (1u << j)) && s->r[j] < earliest) earliest = s->r[j];
        }
        if (earliest > t) t = earliest;
        for (size_t j = 0; j < s->n; j++) {
            if ((mask & (1u << j)) || s->r[j] > t) continue;
            if (best < 0) {
                best = (int)j;
                continue;
            }
            int64_t kj = by_burst ? s->p[j] : s->r[j], kb = by_burst ? s->p[best] : s->r[best];
            if (kj < kb || (kj == kb && (s->r[j] < s->r[best] ||
                                         (s->r[j] == s->r[best] && s->pid[j] < s->pid[best])))) {
                best = (int)j;
            }
        }
        order[i] = best;
        mask |= 1u << best;
        t += s->p[best];
    }
}

typedef struct {
    size_t n;
    size_t order[OPT_NP_MAX];
    size_t pos;
    bool arrived[OPT_NP_MAX];
    const SchedView *view;
} OptNp;

static void *optnp_create(const SchedView *view, const PolicyParams *params, SchedError *err) {
    (void)params;
    if (view->n > OPT_NP_MAX) {
        sched_error_set(err, "opt-np: exact search supports at most %d processes (got %zu)",
                        OPT_NP_MAX, view->n);
        return NULL;
    }
    if (view->cores != 1 || view->cs_cost != 0) {
        sched_error_set(err, "opt-np: supports one core and --cs-cost=0 only");
        return NULL;
    }
    Search *s = calloc(1, sizeof *s);
    OptNp *o = calloc(1, sizeof *o);
    if (s) s->memo_head = malloc(((size_t)1 << view->n) * sizeof *s->memo_head);
    if (!s || !o || !s->memo_head) {
        if (s) free(s->memo_head);
        free(s);
        free(o);
        sched_error_set(err, "out of memory");
        return NULL;
    }
    s->n = view->n;
    for (size_t j = 0; j < s->n; j++) {
        if (view->procs[j].phase_count != 1) {
            free(s->memo_head);
            free(s);
            free(o);
            sched_error_set(err, "opt-np: I/O bursts are not supported");
            return NULL;
        }
        s->r[j] = view->procs[j].arrival;
        s->p[j] = view->procs[j].burst;
        s->pid[j] = view->procs[j].pid;
    }
    memset(s->memo_head, 0xff, ((size_t)1 << s->n) * sizeof *s->memo_head); /* all -1 */
    int fcfs[OPT_NP_MAX], sjf[OPT_NP_MAX];
    greedy(s, false, fcfs);
    greedy(s, true, sjf);
    int64_t tf = order_total(s, fcfs), ts = order_total(s, sjf);
    s->best = ts <= tf ? ts : tf;
    memcpy(s->best_order, ts <= tf ? sjf : fcfs, sizeof s->best_order);
    search(s, 0, 0, 0, 0); /* arrivals are >= 0 */

    o->n = s->n;
    o->view = view;
    for (size_t i = 0; i < s->n; i++) o->order[i] = (size_t)s->best_order[i];
    free(s->memo_head);
    free(s->pool);
    free(s);
    return o;
}

static void optnp_destroy(void *self) { free(self); }

static void optnp_arrival(void *self, size_t p) { ((OptNp *)self)->arrived[p] = true; }

static bool optnp_next_slice(void *self, Slice *out) {
    OptNp *o = self;
    if (o->pos == o->n || !o->arrived[o->order[o->pos]]) return false; /* idle on purpose */
    out->proc = o->order[o->pos++];
    out->length = o->view->remaining[out->proc];
    out->deadline = SCHED_NO_DEADLINE;
    return true;
}

const Policy POLICY_OPT_NP = {
    .name = "opt-np",
    .label = "OPT-NP",
    .heading = "Optimal Non-preemptive Schedule (offline, exact)",
    .preemptive = false,
    .preempt_on_ready = false,
    .uses_quantum = false,
    .uses_estimates = false,
    .work_conserving = false,
    .offline = true,
    .create = optnp_create,
    .destroy = optnp_destroy,
    .on_arrival = optnp_arrival,
    .next_slice = optnp_next_slice,
    .on_preempt = NULL,
};
