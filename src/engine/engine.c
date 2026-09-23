#include "sched/engine.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int64_t arrival;
    int64_t pid;
    size_t index;
} ArrivalKey;

static int cmp_arrival_key(const void *pa, const void *pb) {
    const ArrivalKey *a = pa, *b = pb;
    if (a->arrival != b->arrival) return a->arrival < b->arrival ? -1 : 1;
    return (a->pid > b->pid) - (a->pid < b->pid);
}

typedef enum { APPEND_OK, APPEND_NOMEM, APPEND_LIMIT } AppendResult;

/* Appends [start, end) for proc, extending the last segment instead when it
 * belongs to the same proc and ends exactly at start. */
static AppendResult timeline_append(Timeline *t, int64_t start, int64_t end, size_t proc,
                                    size_t max_segments) {
    if (t->len > 0) {
        Segment *last = &t->items[t->len - 1];
        if (last->proc == proc && last->end == start) {
            last->end = end;
            return APPEND_OK;
        }
    }
    if (t->len >= max_segments) return APPEND_LIMIT;
    if (t->len == t->cap) {
        size_t ncap = t->cap ? t->cap * 2 : 16;
        if (ncap <= t->cap || ncap > SIZE_MAX / sizeof *t->items) return APPEND_NOMEM;
        Segment *items = realloc(t->items, ncap * sizeof *items);
        if (!items) return APPEND_NOMEM;
        t->items = items;
        t->cap = ncap;
    }
    t->items[t->len++] = (Segment){.start = start, .end = end, .proc = proc};
    return APPEND_OK;
}

typedef struct {
    const Workload *w;
    const Policy *policy;
    void *state;
    ArrivalKey *order; /* processes sorted by (arrival, pid) */
    size_t next;       /* first entry of order[] not yet admitted */
    int64_t now;
} Engine;

static void admit_arrivals(Engine *e) {
    while (e->next < e->w->len && e->order[e->next].arrival <= e->now) {
        e->policy->on_arrival(e->state, e->order[e->next].index);
        e->next++;
    }
}

void sim_result_free(SimResult *r) {
    free(r->outcomes);
    free(r->timeline.items);
    memset(r, 0, sizeof *r);
}

SchedStatus sim_run(const Workload *w, const Policy *policy, const PolicyParams *params,
                    SimResult *out, SchedError *err) {
    const SimLimits defaults = {.max_segments = SCHED_DEFAULT_MAX_SEGMENTS};
    return sim_run_limited(w, policy, params, &defaults, out, err);
}

SchedStatus sim_run_limited(const Workload *w, const Policy *policy, const PolicyParams *params,
                            const SimLimits *limits, SimResult *out, SchedError *err) {
    memset(out, 0, sizeof *out);
    SchedStatus st = workload_validate(w, err);
    if (st != SCHED_OK) return st;
    if (policy->uses_quantum && params->quantum <= 0) {
        sched_error_set(err, "%s needs a quantum > 0", policy->name);
        return SCHED_E_USAGE;
    }

    const size_t n = w->len;
    out->policy = policy;
    out->workload = w;
    out->outcomes = malloc(n * sizeof *out->outcomes);
    int64_t *remaining = malloc(n * sizeof *remaining);
    Engine e = {.w = w, .policy = policy, .order = malloc(n * sizeof(ArrivalKey))};
    if (!out->outcomes || !remaining || !e.order) goto nomem;

    for (size_t i = 0; i < n; i++) {
        remaining[i] = w->items[i].burst;
        out->outcomes[i] = (ProcOutcome){.start = -1, .completion = -1};
        e.order[i] = (ArrivalKey){w->items[i].arrival, w->items[i].pid, i};
    }
    qsort(e.order, n, sizeof *e.order, cmp_arrival_key);

    const SchedView view = {.procs = w->items, .remaining = remaining, .n = n};
    e.state = policy->create(&view, params);
    if (!e.state) goto nomem;

    /* The timeline starts at the first arrival. workload_validate guarantees
     * now + slice never exceeds max(arrival) + sum(burst) <= INT64_MAX. */
    e.now = e.order[0].arrival;
    admit_arrivals(&e);
    size_t completed = 0;
    AppendResult appended;
    while (completed < n) {
        Slice s;
        if (!policy->next_slice(e.state, &s)) {
            assert(e.next < n && "policy reported nothing ready with no future arrivals");
            int64_t until = e.order[e.next].arrival;
            appended =
                timeline_append(&out->timeline, e.now, until, SCHED_IDLE, limits->max_segments);
            if (appended != APPEND_OK) goto append_failed;
            e.now = until;
            admit_arrivals(&e);
            continue;
        }
        const size_t p = s.proc;
        assert(p < n && remaining[p] > 0 && s.length > 0);

        int64_t len = s.length < remaining[p] ? s.length : remaining[p];
        if (policy->preempt_on_arrival && e.next < n && e.order[e.next].arrival - e.now < len) {
            len = e.order[e.next].arrival - e.now;
        }
        if (out->outcomes[p].start < 0) out->outcomes[p].start = e.now;
        appended = timeline_append(&out->timeline, e.now, e.now + len, p, limits->max_segments);
        if (appended != APPEND_OK) goto append_failed;
        e.now += len;
        remaining[p] -= len;

        /* Arrivals up to and including `now` are admitted before the slice
         * outcome is reported, so under RR they queue ahead of the process
         * that was just preempted. */
        admit_arrivals(&e);
        if (remaining[p] == 0) {
            out->outcomes[p].completion = e.now;
            completed++;
            if (policy->on_complete) policy->on_complete(e.state, p);
        } else {
            assert(policy->on_preempt && "run-to-completion policy was preempted");
            policy->on_preempt(e.state, p);
        }
    }

    policy->destroy(e.state);
    free(remaining);
    free(e.order);
    return SCHED_OK;

append_failed:
    policy->destroy(e.state);
    free(remaining);
    free(e.order);
    sim_result_free(out);
    if (appended == APPEND_LIMIT) {
        sched_error_set(err,
                        "%s: schedule needs more than %zu Gantt segments; use a larger quantum "
                        "or a smaller workload",
                        policy->name, limits->max_segments);
        return SCHED_E_INPUT;
    }
    sched_error_set(err, "out of memory while simulating %s", policy->name);
    return SCHED_E_NOMEM;

nomem:
    free(remaining);
    free(e.order);
    sim_result_free(out);
    sched_error_set(err, "out of memory while simulating %s", policy->name);
    return SCHED_E_NOMEM;
}
