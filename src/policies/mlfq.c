/* Multi-Level Feedback Queue, following the rules in OSTEP chapter 8.
 *
 *   1. A job in a higher queue (lower level number) runs before any job in a
 *      lower queue; arrivals and I/O completions preempt lower-level jobs.
 *   2. Jobs in the same queue share the CPU round-robin with that level's
 *      time slice (--mlfq-quanta).
 *   3. A new job enters the top queue.
 *   4. Once a job has used its level's time allotment (--mlfq-allot),
 *      however many times it gave up the CPU, it moves down one level; so
 *      yielding just before the slice expires cannot keep a job at the top.
 *      The bottom level has no allotment.
 *   5. Every S ticks (--mlfq-boost, at t = S, 2S, ...), every job moves to
 *      the top queue with a fresh allotment.
 *
 * Details this model has to pin down:
 *   - A job interrupted before its slice ends (by a higher-level arrival or a
 *     switch-in recheck) goes to the FRONT of its queue and keeps the rest of
 *     its slice, so an equal-level arrival never displaces it.
 *   - A slice that ends by exhausting the quantum sends the job to the back of
 *     its queue (after anything that became ready at the same instant).
 *   - At a boost, the queues are concatenated top to bottom into the top
 *     queue; the running job's slice ends and it joins the back of the top
 *     queue. Per-job levels are reset lazily (O(levels) per boost).
 *   - A job returning from I/O keeps its level and used allotment and joins
 *     the back of its queue with a fresh slice.
 */
#include <stdlib.h>

#include "sched/policy.h"

#define NIL SIZE_MAX

typedef struct {
    const SchedView *view;
    size_t levels;
    int64_t quanta[SCHED_MLFQ_MAX_LEVELS];
    int64_t allot[SCHED_MLFQ_MAX_LEVELS];
    int64_t boost;
    int64_t epoch; /* boosts applied so far: floor(now / boost) */
    size_t head[SCHED_MLFQ_MAX_LEVELS], tail[SCHED_MLFQ_MAX_LEVELS];
    size_t *next, *prev; /* doubly linked queues over process indices */
    size_t *level;
    int64_t *used;       /* allotment used at the current level */
    int64_t *slice_left; /* rest of an interrupted slice; 0 = fresh */
    int64_t *seen_epoch; /* boost epoch the per-job fields belong to */
} Mlfq;

static void *mlfq_create(const SchedView *view, const PolicyParams *params, SchedError *err) {
    Mlfq *s = calloc(1, sizeof *s);
    size_t n = view->n;
    if (s) {
        s->next = malloc(n * sizeof *s->next);
        s->prev = malloc(n * sizeof *s->prev);
        s->level = calloc(n, sizeof *s->level);
        s->used = calloc(n, sizeof *s->used);
        s->slice_left = calloc(n, sizeof *s->slice_left);
        s->seen_epoch = calloc(n, sizeof *s->seen_epoch);
    }
    if (!s || !s->next || !s->prev || !s->level || !s->used || !s->slice_left || !s->seen_epoch) {
        if (s) {
            free(s->next);
            free(s->prev);
            free(s->level);
            free(s->used);
            free(s->slice_left);
            free(s->seen_epoch);
        }
        free(s);
        sched_error_set(err, "out of memory");
        return NULL;
    }
    s->view = view;
    s->levels = params->mlfq_levels;
    for (size_t l = 0; l < s->levels; l++) {
        s->quanta[l] = params->mlfq_quanta[l];
        s->allot[l] = params->mlfq_allotment[l];
        s->head[l] = s->tail[l] = NIL;
    }
    s->boost = params->mlfq_boost;
    s->epoch = s->boost > 0 ? view->now / s->boost : 0;
    for (size_t i = 0; i < n; i++) s->seen_epoch[i] = s->epoch;
    return s;
}

static void mlfq_destroy(void *self) {
    Mlfq *s = self;
    free(s->next);
    free(s->prev);
    free(s->level);
    free(s->used);
    free(s->slice_left);
    free(s->seen_epoch);
    free(s);
}

static void push_back(Mlfq *s, size_t l, size_t p) {
    s->next[p] = NIL;
    s->prev[p] = s->tail[l];
    if (s->tail[l] == NIL) {
        s->head[l] = p;
    } else {
        s->next[s->tail[l]] = p;
    }
    s->tail[l] = p;
}

static void push_front(Mlfq *s, size_t l, size_t p) {
    s->prev[p] = NIL;
    s->next[p] = s->head[l];
    if (s->head[l] == NIL) {
        s->tail[l] = p;
    } else {
        s->prev[s->head[l]] = p;
    }
    s->head[l] = p;
}

static size_t pop_front(Mlfq *s, size_t l) {
    size_t p = s->head[l];
    s->head[l] = s->next[p];
    if (s->head[l] == NIL) {
        s->tail[l] = NIL;
    } else {
        s->prev[s->head[l]] = NIL;
    }
    return p;
}

/* Applies any boost due at or before now: queues are concatenated top to
 * bottom into the top queue; per-job state is reset lazily by sync(). */
static void maybe_boost(Mlfq *s) {
    if (s->boost <= 0) return;
    int64_t epoch = s->view->now / s->boost;
    if (epoch == s->epoch) return;
    s->epoch = epoch;
    for (size_t l = 1; l < s->levels; l++) {
        if (s->head[l] == NIL) continue;
        if (s->tail[0] == NIL) {
            s->head[0] = s->head[l];
        } else {
            s->next[s->tail[0]] = s->head[l];
            s->prev[s->head[l]] = s->tail[0];
        }
        s->tail[0] = s->tail[l];
        s->head[l] = s->tail[l] = NIL;
    }
}

/* Brings a job's level/allotment up to date with boosts it has missed.
 * Returns true if a boost reset it. */
static bool sync(Mlfq *s, size_t p) {
    if (s->seen_epoch[p] == s->epoch) return false;
    s->seen_epoch[p] = s->epoch;
    s->level[p] = 0;
    s->used[p] = 0;
    s->slice_left[p] = 0;
    return true;
}

static void mlfq_arrival(void *self, size_t p) {
    Mlfq *s = self;
    maybe_boost(s);
    sync(s, p);
    s->level[p] = 0;
    s->used[p] = 0;
    s->slice_left[p] = 0;
    push_back(s, 0, p);
}

/* Charges `ran` ticks of CPU to p at its level; demotes when the allotment is
 * used up. Returns true if p was demoted. */
static bool charge(Mlfq *s, size_t p, int64_t ran) {
    size_t l = s->level[p];
    s->used[p] += ran;
    if (l + 1 < s->levels && s->used[p] >= s->allot[l]) {
        s->level[p] = l + 1;
        s->used[p] = 0;
        return true;
    }
    return false;
}

static void mlfq_preempt(void *self, size_t p, int64_t ran) {
    Mlfq *s = self;
    maybe_boost(s);
    if (sync(s, p)) { /* the slice was cut short by a boost */
        push_back(s, 0, p);
        return;
    }
    s->slice_left[p] -= ran;
    if (charge(s, p, ran) || s->slice_left[p] == 0) {
        s->slice_left[p] = 0;
        push_back(s, s->level[p], p);
    } else {
        push_front(s, s->level[p], p); /* interrupted: keeps the rest of its slice */
    }
}

static void mlfq_block(void *self, size_t p, int64_t ran) {
    Mlfq *s = self;
    maybe_boost(s);
    if (!sync(s, p)) charge(s, p, ran);
    s->slice_left[p] = 0;
}

static void mlfq_ready(void *self, size_t p) {
    Mlfq *s = self;
    maybe_boost(s);
    sync(s, p);
    s->slice_left[p] = 0;
    push_back(s, s->level[p], p);
}

static bool mlfq_next_slice(void *self, Slice *out) {
    Mlfq *s = self;
    maybe_boost(s);
    size_t l = 0;
    while (l < s->levels && s->head[l] == NIL) l++;
    if (l == s->levels) return false;
    size_t p = pop_front(s, l);
    sync(s, p);
    if (s->slice_left[p] == 0) s->slice_left[p] = s->quanta[l];
    out->proc = p;
    out->length = s->slice_left[p];
    if (l + 1 < s->levels && s->allot[l] - s->used[p] < out->length) {
        out->length = s->allot[l] - s->used[p];
    }
    s->slice_left[p] = out->length;
    out->deadline = SCHED_NO_DEADLINE;
    if (s->boost > 0 && s->epoch < INT64_MAX / s->boost) out->deadline = (s->epoch + 1) * s->boost;
    return true;
}

const Policy POLICY_MLFQ = {
    .name = "mlfq",
    .label = "MLFQ",
    .heading = "MLFQ (Multi-Level Feedback Queue) Scheduling",
    .preemptive = true,
    .preempt_on_ready = true,
    .uses_quantum = false,
    .uses_estimates = false,
    .work_conserving = true,
    .create = mlfq_create,
    .destroy = mlfq_destroy,
    .on_arrival = mlfq_arrival,
    .on_ready = mlfq_ready,
    .next_slice = mlfq_next_slice,
    .on_preempt = mlfq_preempt,
    .on_block = mlfq_block,
};
