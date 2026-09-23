#include "sched/engine.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "sched/checked.h"
#include "util/heap.h"

void sim_options_default(SimOptions *o) {
    *o = (SimOptions){.cores = 1,
                      .cs_cost = 0,
                      .predict = PREDICT_ORACLE,
                      .alpha = 0.5,
                      .tau0 = 5.0,
                      .max_segments = SCHED_DEFAULT_MAX_SEGMENTS};
}

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

typedef struct {
    size_t proc;        /* running or switching-in process; SCHED_NONE if free */
    bool in_cs;         /* switching in `proc` until busy_until */
    bool recheck;       /* a ready event happened during the switch-in */
    int64_t busy_until; /* end of the switch or of the run slice */
    int64_t run_start;  /* start of the current run slice */
    int64_t run_end;    /* when the slice ends if its deadline does not cut it */
    int64_t length;     /* requested slice length */
    int64_t deadline;   /* absolute slice deadline */
    size_t context;     /* last process dispatched on this core */
    int64_t free_since; /* when the core last became free */
} Core;

typedef enum { ENGINE_OK, ENGINE_NOMEM, ENGINE_LIMIT, ENGINE_OVERFLOW } EngineFail;

typedef struct {
    const Workload *w;
    const Policy *policy;
    const SimOptions *opt;
    void *state;
    SimResult *out;
    SchedView view;
    ArrivalKey *order; /* processes sorted by (arrival, pid) */
    size_t next;       /* first entry of order[] not yet admitted */
    int64_t *remaining;
    int64_t *burst_len;
    double *predicted; /* NULL under the oracle */
    size_t *phase;     /* index of each process's current CPU phase */
    int64_t *wake;     /* I/O completion time while blocked */
    Heap blocked;      /* blocked processes by (wake, pid) */
    Core *cores;
    Slice *picks;
    bool *core_taken; /* dispatch scratch, one per core */
    bool *pick_placed;
    size_t segments; /* total over all cores */
    size_t completed;
    EngineFail fail;
} Engine;

static bool blocked_less(const void *ctx, size_t a, size_t b) {
    const Engine *e = ctx;
    if (e->wake[a] != e->wake[b]) return e->wake[a] < e->wake[b];
    return e->w->items[a].pid < e->w->items[b].pid;
}

/* Appends [start, end) to a core's timeline, merging with the previous
 * segment when kind and proc match and the two touch. */
static void append(Engine *e, size_t core, int64_t start, int64_t end, size_t proc,
                   SegmentKind kind) {
    if (start >= end || e->fail != ENGINE_OK) return;
    Timeline *t = &e->out->timelines[core];
    if (t->len > 0) {
        Segment *last = &t->items[t->len - 1];
        if (last->kind == kind && last->proc == proc && last->end == start) {
            last->end = end;
            return;
        }
    }
    if (e->segments >= e->opt->max_segments) {
        e->fail = ENGINE_LIMIT;
        return;
    }
    if (t->len == t->cap) {
        size_t ncap = t->cap ? t->cap * 2 : 16;
        Segment *items = NULL;
        if (ncap > t->cap && ncap <= SIZE_MAX / sizeof *items) {
            items = realloc(t->items, ncap * sizeof *items);
        }
        if (!items) {
            e->fail = ENGINE_NOMEM;
            return;
        }
        t->items = items;
        t->cap = ncap;
    }
    t->items[t->len++] = (Segment){.start = start, .end = end, .proc = proc, .kind = kind};
    e->segments++;
}

static int64_t add_time(Engine *e, int64_t a, int64_t b) {
    int64_t r;
    if (!ck_add_i64(a, b, &r)) {
        e->fail = ENGINE_OVERFLOW;
        return INT64_MAX;
    }
    return r;
}

static bool admit_arrivals(Engine *e) {
    bool any = false;
    while (e->next < e->w->len && e->order[e->next].arrival <= e->view.now) {
        e->policy->on_arrival(e->state, e->order[e->next].index);
        e->next++;
        any = true;
    }
    return any;
}

static bool wake_blocked(Engine *e) {
    bool any = false;
    while (!heap_empty(&e->blocked) && e->wake[e->blocked.items[0]] <= e->view.now) {
        size_t p = heap_pop(&e->blocked);
        if (e->policy->on_ready) {
            e->policy->on_ready(e->state, p);
        } else {
            e->policy->on_arrival(e->state, p);
        }
        any = true;
    }
    return any;
}

/* The current CPU burst of p just finished at now. */
static void burst_done(Engine *e, size_t p, int64_t ran) {
    const Process *proc = &e->w->items[p];
    const int64_t *ph = workload_phases(e->w, p);
    size_t k = e->phase[p];
    if (e->predicted) {
        e->predicted[p] =
            e->opt->alpha * (double)e->burst_len[p] + (1.0 - e->opt->alpha) * e->predicted[p];
    }
    if (k + 1 == proc->phase_count) {
        e->out->outcomes[p].completion = e->view.now;
        e->completed++;
        if (e->policy->on_complete) e->policy->on_complete(e->state, p, ran);
        return;
    }
    e->wake[p] = add_time(e, e->view.now, ph[k + 1]);
    e->phase[p] = k + 2;
    e->burst_len[p] = e->remaining[p] = ph[k + 2];
    heap_push(&e->blocked, p);
    if (e->policy->on_block) e->policy->on_block(e->state, p, ran);
}

/* Ends the run slice on core c at now (naturally or by interruption).
 * Returns true if the process went back to the ready set. */
static bool end_run(Engine *e, size_t c) {
    Core *core = &e->cores[c];
    size_t p = core->proc;
    int64_t ran = e->view.now - core->run_start;
    append(e, c, core->run_start, e->view.now, p, SEG_RUN);
    e->remaining[p] -= ran;
    core->proc = SCHED_NONE;
    core->free_since = e->view.now;
    if (e->remaining[p] == 0) {
        burst_done(e, p, ran);
        return false;
    }
    assert(e->policy->on_preempt && "run-to-completion policy was preempted");
    e->policy->on_preempt(e->state, p, ran);
    return true;
}

static void begin_run(Engine *e, size_t c) {
    Core *core = &e->cores[c];
    size_t p = core->proc;
    int64_t now = e->view.now;
    core->in_cs = false;
    core->run_start = now;
    if (e->out->outcomes[p].start < 0) e->out->outcomes[p].start = now;
    int64_t len = core->length < e->remaining[p] ? core->length : e->remaining[p];
    core->run_end = add_time(e, now, len);
    core->busy_until = core->deadline < core->run_end ? core->deadline : core->run_end;
}

static void dispatch_one(Engine *e, size_t c, const Slice *s) {
    Core *core = &e->cores[c];
    int64_t now = e->view.now;
    assert(s->proc < e->w->len && e->remaining[s->proc] > 0 && s->length > 0 && s->deadline > now);
    append(e, c, core->free_since, now, SCHED_IDLE, SEG_IDLE);
    core->proc = s->proc;
    core->length = s->length;
    core->deadline = s->deadline;
    core->recheck = false;
    bool switching = core->context != SCHED_NONE && core->context != s->proc;
    core->context = s->proc;
    if (switching) e->out->switches++;
    if (switching && e->opt->cs_cost > 0) {
        core->in_cs = true;
        core->busy_until = add_time(e, now, e->opt->cs_cost);
        append(e, c, now, core->busy_until, s->proc, SEG_CS);
        e->out->cs_time += e->opt->cs_cost;
        return;
    }
    begin_run(e, c);
}

/* Offers every free core to the policy, keeping a process on the core it
 * last used when that core is free. */
static void dispatch(Engine *e) {
    size_t ncores = e->opt->cores, nfree = 0, npicks = 0;
    for (size_t c = 0; c < ncores; c++) nfree += e->cores[c].proc == SCHED_NONE;
    while (npicks < nfree && e->policy->next_slice(e->state, &e->picks[npicks])) npicks++;
    if (npicks == 0) return;
    bool *used = e->core_taken, *placed = e->pick_placed;
    memset(used, 0, ncores * sizeof *used);
    memset(placed, 0, ncores * sizeof *placed);
    for (size_t i = 0; i < npicks && ncores > 1; i++) {
        for (size_t c = 0; c < ncores; c++) {
            if (!used[c] && e->cores[c].proc == SCHED_NONE &&
                e->cores[c].context == e->picks[i].proc) {
                used[c] = placed[i] = true;
                dispatch_one(e, c, &e->picks[i]);
                break;
            }
        }
    }
    size_t c = 0;
    for (size_t i = 0; i < npicks; i++) {
        if (placed[i]) continue;
        while (used[c] || e->cores[c].proc != SCHED_NONE) c++;
        used[c] = true;
        dispatch_one(e, c, &e->picks[i]);
    }
}

/* Handles every event at view.now, in the order documented in policy.h. */
static void process_instant(Engine *e) {
    const int64_t now = e->view.now;
    const size_t ncores = e->opt->cores;
    bool cut = false; /* a deadline (on a running or switching core) is due now */
    for (size_t c = 0; c < ncores; c++) {
        cut |= e->cores[c].proc != SCHED_NONE && e->cores[c].deadline == now;
    }
    bool ready = admit_arrivals(e);
    ready |= wake_blocked(e);
    bool requeued = false;
    for (size_t c = 0; c < ncores; c++) { /* slices that ran their full length */
        Core *core = &e->cores[c];
        if (core->proc != SCHED_NONE && !core->in_cs && core->run_end == now) {
            requeued |= end_run(e, c);
        }
    }
    /* For preempt_on_ready policies the running set is re-chosen globally
     * whenever the ready set gained a process (arrival, I/O completion, or a
     * slice handed back on any core) or a deadline fell due. Cores that are
     * still switching in cannot be interrupted; they are marked instead. */
    bool redecide = e->policy->preempt_on_ready && (ready || requeued || cut);
    for (size_t c = 0; c < ncores; c++) {
        if (redecide && e->cores[c].proc != SCHED_NONE && e->cores[c].in_cs) {
            e->cores[c].recheck = true;
        }
    }
    for (size_t c = 0; c < ncores; c++) { /* switch-ins that finished */
        Core *core = &e->cores[c];
        if (core->proc == SCHED_NONE || !core->in_cs || core->busy_until != now) continue;
        /* The switched-in process always runs at least one tick, so switching
         * cannot livelock; a reason to re-decide that arose during the switch
         * takes effect after that tick. */
        if (core->recheck || core->deadline <= now) core->deadline = add_time(e, now, 1);
        begin_run(e, c);
    }
    /* Interruptions: slices cut by their deadline, and every running slice
     * when re-deciding. */
    for (size_t c = 0; c < ncores; c++) {
        Core *core = &e->cores[c];
        if (core->proc == SCHED_NONE || core->in_cs || core->run_start == now) continue;
        if (redecide || core->busy_until == now) end_run(e, c);
    }
}

static int64_t next_event(const Engine *e) {
    int64_t t = INT64_MAX;
    if (e->next < e->w->len) t = e->order[e->next].arrival;
    if (!heap_empty(&e->blocked) && e->wake[e->blocked.items[0]] < t) {
        t = e->wake[e->blocked.items[0]];
    }
    for (size_t c = 0; c < e->opt->cores; c++) {
        const Core *core = &e->cores[c];
        if (core->proc == SCHED_NONE) continue;
        if (core->busy_until < t) t = core->busy_until;
        /* A deadline marks a change in the policy's view (e.g. aging), so it
         * is an event even while its core is still switching in. */
        if (core->deadline > e->view.now && core->deadline < t) t = core->deadline;
    }
    return t;
}

void sim_result_free(SimResult *r) {
    if (r->timelines) {
        for (size_t c = 0; c < r->cores; c++) free(r->timelines[c].items);
    }
    free(r->timelines);
    free(r->outcomes);
    memset(r, 0, sizeof *r);
}

static SchedStatus check_options(const Workload *w, const Policy *policy,
                                 const PolicyParams *params, const SimOptions *o, SchedError *err) {
    if (policy->uses_quantum && params->quantum <= 0) {
        sched_error_set(err, "%s needs a quantum > 0", policy->name);
        return SCHED_E_USAGE;
    }
    if (o->cores < 1 || o->cores > SCHED_MAX_CORES) {
        sched_error_set(err, "number of cores must be in 1..%d", SCHED_MAX_CORES);
        return SCHED_E_USAGE;
    }
    if (o->max_segments < 1) {
        sched_error_set(err, "segment limit must be >= 1");
        return SCHED_E_USAGE;
    }
    if (o->predict == PREDICT_EWMA &&
        (!(o->alpha >= 0.0 && o->alpha <= 1.0) || !(o->tau0 > 0.0) || !isfinite(o->tau0))) {
        sched_error_set(err, "EWMA needs 0 <= alpha <= 1 and a finite tau0 > 0");
        return SCHED_E_USAGE;
    }
    /* Each switch adds a segment, so max_segments bounds the switch overhead. */
    uint64_t room = (uint64_t)(INT64_MAX - workload_horizon(w));
    if (o->cs_cost < 0 || (o->cs_cost > 0 && (uint64_t)o->cs_cost > room / o->max_segments)) {
        sched_error_set(err, "context-switch cost must be >= 0 and small enough that the "
                             "schedule fits in 64-bit time");
        return SCHED_E_USAGE;
    }
    return SCHED_OK;
}

SchedStatus sim_run(const Workload *w, const Policy *policy, const PolicyParams *params,
                    const SimOptions *options, SimResult *out, SchedError *err) {
    memset(out, 0, sizeof *out);
    SchedStatus st = workload_validate(w, err);
    if (st == SCHED_OK) st = check_options(w, policy, params, options, err);
    if (st != SCHED_OK) return st;

    const size_t n = w->len, ncores = options->cores;
    out->policy = policy;
    out->workload = w;
    out->params = *params;
    out->options = *options;
    out->cores = ncores;
    out->outcomes = malloc(n * sizeof *out->outcomes);
    out->timelines = calloc(ncores, sizeof *out->timelines);

    Engine e = {.w = w, .policy = policy, .opt = options, .out = out};
    e.order = malloc(n * sizeof *e.order);
    e.remaining = malloc(n * sizeof *e.remaining);
    e.burst_len = malloc(n * sizeof *e.burst_len);
    e.phase = calloc(n, sizeof *e.phase);
    e.wake = malloc(n * sizeof *e.wake);
    e.cores = malloc(ncores * sizeof *e.cores);
    e.picks = malloc(ncores * sizeof *e.picks);
    e.core_taken = malloc(ncores * sizeof *e.core_taken);
    e.pick_placed = malloc(ncores * sizeof *e.pick_placed);
    if (options->predict == PREDICT_EWMA) e.predicted = malloc(n * sizeof *e.predicted);
    bool heap_ok = heap_init(&e.blocked, n, blocked_less, &e);
    if (!out->outcomes || !out->timelines || !e.order || !e.remaining || !e.burst_len || !e.phase ||
        !e.wake || !e.cores || !e.picks || !e.core_taken || !e.pick_placed || !heap_ok ||
        (options->predict == PREDICT_EWMA && !e.predicted)) {
        e.fail = ENGINE_NOMEM;
        goto done;
    }

    for (size_t i = 0; i < n; i++) {
        e.remaining[i] = e.burst_len[i] = workload_phases(w, i)[0];
        if (e.predicted) e.predicted[i] = options->tau0;
        out->outcomes[i] = (ProcOutcome){.start = -1, .completion = -1};
        e.order[i] = (ArrivalKey){w->items[i].arrival, w->items[i].pid, i};
    }
    qsort(e.order, n, sizeof *e.order, cmp_arrival_key);
    out->first_arrival = e.order[0].arrival;
    for (size_t c = 0; c < ncores; c++) {
        e.cores[c] =
            (Core){.proc = SCHED_NONE, .context = SCHED_NONE, .free_since = out->first_arrival};
    }

    e.view = (SchedView){.procs = w->items,
                         .n = n,
                         .cores = ncores,
                         .cs_cost = options->cs_cost,
                         .now = out->first_arrival,
                         .remaining = e.remaining,
                         .burst_len = e.burst_len,
                         .predicted = e.predicted};
    e.state = policy->create(&e.view, params, err);
    if (!e.state) {
        st = SCHED_E_USAGE;
        goto cleanup;
    }

    process_instant(&e);
    while (e.completed < n && e.fail == ENGINE_OK) {
        dispatch(&e);
        int64_t t = next_event(&e);
        assert(t != INT64_MAX && "no runnable process and no future event");
        e.view.now = t;
        process_instant(&e);
    }
    for (size_t c = 0; c < ncores; c++) {
        append(&e, c, e.cores[c].free_since, e.view.now, SCHED_IDLE, SEG_IDLE);
    }

done:
    switch (e.fail) {
    case ENGINE_OK:
        break;
    case ENGINE_LIMIT:
        sched_error_set(err,
                        "%s: schedule needs more than %zu Gantt segments; use a larger quantum "
                        "or a smaller workload",
                        policy->name, options->max_segments);
        st = SCHED_E_INPUT;
        break;
    case ENGINE_OVERFLOW:
        sched_error_set(err, "%s: simulated time overflows a 64-bit tick counter", policy->name);
        st = SCHED_E_INPUT;
        break;
    case ENGINE_NOMEM:
        sched_error_set(err, "out of memory while simulating %s", policy->name);
        st = SCHED_E_NOMEM;
        break;
    }
cleanup:
    if (e.state) policy->destroy(e.state);
    if (heap_ok) heap_free(&e.blocked);
    free(e.order);
    free(e.remaining);
    free(e.burst_len);
    free(e.predicted);
    free(e.phase);
    free(e.wake);
    free(e.cores);
    free(e.picks);
    free(e.core_taken);
    free(e.pick_placed);
    if (st != SCHED_OK) sim_result_free(out);
    return st;
}
