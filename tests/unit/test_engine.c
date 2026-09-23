/* Engine, registry and metrics unit tests. Every expected timeline below was
 * worked out by hand from the rules in include/sched/policy.h. */
#include "check.h"
#include "sched/engine.h"
#include "sched/metrics.h"

static SimOptions defaults(void) {
    SimOptions o;
    sim_options_default(&o);
    return o;
}

static SimResult run_opts(const Workload *w, const char *name, int64_t quantum,
                          const SimOptions *o) {
    SimResult r;
    PolicyParams params;
    policy_params_default(&params);
    params.quantum = quantum;
    SchedError err;
    if (sim_run(w, policy_find(name), &params, o, &r, &err) != SCHED_OK) {
        fprintf(stderr, "sim_run(%s) failed: %s\n", name, err.msg);
        exit(EXIT_FAILURE);
    }
    return r;
}

static SimResult run(const Workload *w, const char *name, int64_t quantum) {
    SimOptions o = defaults();
    return run_opts(w, name, quantum, &o);
}

/* Expected segment: pid -1 = idle; cs = true for a switch into pid. */
typedef struct {
    int64_t start, end, pid;
    bool cs;
} Seg;

static void check_core(const SimResult *r, size_t core, const Seg *exp, size_t n) {
    const Timeline *t = &r->timelines[core];
    CHECK_EQ_I64(t->len, n);
    for (size_t i = 0; i < n && i < t->len; i++) {
        const Segment *s = &t->items[i];
        CHECK_EQ_I64(s->start, exp[i].start);
        CHECK_EQ_I64(s->end, exp[i].end);
        if (exp[i].pid < 0) {
            CHECK(s->kind == SEG_IDLE && s->proc == SCHED_IDLE);
        } else {
            CHECK(s->kind == (exp[i].cs ? SEG_CS : SEG_RUN));
            if (s->proc != SCHED_IDLE) CHECK_EQ_I64(r->workload->items[s->proc].pid, exp[i].pid);
        }
    }
}

static void check_timeline(const SimResult *r, const Seg *exp, size_t n) {
    CHECK_EQ_I64(r->cores, 1);
    check_core(r, 0, exp, n);
}

static Workload with_phases(const int64_t *const *phases, const size_t *counts,
                            const int64_t *arrivals, size_t n) {
    Workload w;
    workload_init(&w);
    for (size_t i = 0; i < n; i++) {
        Process p = process_make((int64_t)i + 1, arrivals[i], 0);
        if (workload_push_phases(&w, p, phases[i], counts[i], NULL) != SCHED_OK) exit(1);
    }
    return w;
}

static void test_registry(void) {
    CHECK(policy_count() >= 4);
    CHECK_STR_EQ(policy_at(0)->name, "fcfs");
    CHECK_STR_EQ(policy_at(3)->name, "rr");
    CHECK(policy_at(policy_count()) == NULL);
    CHECK(policy_find("srtf") == policy_at(2));
    CHECK(policy_find("SRTF") == NULL);
    CHECK(policy_find("nonexistent") == NULL);
    for (size_t i = 0; i < policy_count(); i++) {
        const Policy *p = policy_at(i);
        CHECK(p->create && p->destroy && p->on_arrival && p->next_slice);
        CHECK(!p->preemptive == !p->on_preempt); /* preemptive policies must requeue */
        CHECK(!p->preempt_on_ready || p->preemptive);
    }
}

/* Idle gap at the start and in the middle; timeline begins at first arrival. */
static void test_idle_gaps(void) {
    Triple p[] = {{1, 2, 3}, {2, 7, 2}, {3, 8, 1}};
    Workload w = workload_of(p, 3);
    const char *names[] = {"fcfs", "sjf", "srtf", "rr"};
    const Seg exp[] = {{2, 5, 1, false}, {5, 7, -1, false}, {7, 9, 2, false}, {9, 10, 3, false}};
    for (size_t i = 0; i < 4; i++) {
        SimResult r = run(&w, names[i], 2);
        check_timeline(&r, exp, 4);
        CHECK_EQ_I64(metrics_makespan(&r), 10);
        CHECK_EQ_I64(r.first_arrival, 2);
        CHECK_EQ_I64(r.switches, 2);
        CHECK_EQ_I64(r.cs_time, 0);
        sim_result_free(&r);
    }
    workload_free(&w);
}

static void test_srtf_preempts_on_arrival(void) {
    Triple p[] = {{1, 0, 8}, {2, 3, 2}, {3, 4, 5}};
    Workload w = workload_of(p, 3);
    SimResult r = run(&w, "srtf", 2);
    const Seg exp[] = {{0, 3, 1, false}, {3, 5, 2, false}, {5, 10, 1, false}, {10, 15, 3, false}};
    check_timeline(&r, exp, 4);
    CHECK_EQ_I64(r.outcomes[0].start, 0);
    CHECK_EQ_I64(r.outcomes[0].completion, 10);
    CHECK_EQ_I64(r.outcomes[2].start, 10);
    sim_result_free(&r);
    workload_free(&w);
}

/* A process arriving during a slice and one arriving exactly at its end are
 * both queued ahead of the preempted process. */
static void test_rr_arrival_ordering(void) {
    Triple p[] = {{1, 0, 4}, {2, 2, 2}, {3, 1, 1}};
    Workload w = workload_of(p, 3);
    SimResult r = run(&w, "rr", 2);
    const Seg exp[] = {{0, 2, 1, false}, {2, 3, 3, false}, {3, 5, 2, false}, {5, 7, 1, false}};
    check_timeline(&r, exp, 4);
    sim_result_free(&r);
    workload_free(&w);
}

static void test_rr_coalesces_consecutive_slices(void) {
    Triple p[] = {{5, 0, 7}};
    Workload w = workload_of(p, 1);
    SimResult r = run(&w, "rr", 1);
    const Seg exp[] = {{0, 7, 5, false}};
    check_timeline(&r, exp, 1);
    CHECK_EQ_I64(r.switches, 0);
    sim_result_free(&r);
    workload_free(&w);
}

static void test_large_values_do_not_overflow(void) {
    Triple p[] = {{1, 0, 2000000000}, {2, 0, 2000000000}};
    Workload w = workload_of(p, 2);
    SimResult r = run(&w, "fcfs", 2);
    CHECK_EQ_I64(r.outcomes[1].completion, 4000000000);
    sim_result_free(&r);

    r = run(&w, "rr", 1000000000);
    CHECK_EQ_I64(r.outcomes[0].completion, 3000000000);
    CHECK_EQ_I64(r.outcomes[1].completion, 4000000000);
    sim_result_free(&r);
    workload_free(&w);

    Triple edge[] = {{1, INT64_MAX - 10, 4}, {2, 0, 6}};
    w = workload_of(edge, 2);
    r = run(&w, "srtf", 1);
    CHECK_EQ_I64(r.outcomes[1].completion, 6);
    CHECK_EQ_I64(r.outcomes[0].completion, INT64_MAX - 6);
    sim_result_free(&r);
    workload_free(&w);
}

static void test_run_errors(void) {
    SimResult r;
    SchedError err;
    PolicyParams params;
    policy_params_default(&params);
    SimOptions o = defaults();
    Triple dup[] = {{1, 0, 1}, {1, 1, 1}};
    Workload w = workload_of(dup, 2);
    CHECK(sim_run(&w, policy_find("fcfs"), &params, &o, &r, &err) == SCHED_E_INPUT);
    CHECK_CONTAINS(err.msg, "duplicate PID 1");
    CHECK(r.outcomes == NULL && r.timelines == NULL);
    workload_free(&w);

    Triple ok[] = {{1, 0, 1}};
    w = workload_of(ok, 1);
    params.quantum = 0;
    CHECK(sim_run(&w, policy_find("rr"), &params, &o, &r, &err) == SCHED_E_USAGE);
    CHECK_CONTAINS(err.msg, "quantum");
    CHECK(sim_run(&w, policy_find("fcfs"), &params, &o, &r, &err) == SCHED_OK); /* unused */
    sim_result_free(&r);
    params.quantum = 2;

    o.cores = 0;
    CHECK(sim_run(&w, policy_find("fcfs"), &params, &o, &r, &err) == SCHED_E_USAGE);
    CHECK_CONTAINS(err.msg, "number of cores");
    o = defaults();
    o.max_segments = 0;
    CHECK(sim_run(&w, policy_find("fcfs"), &params, &o, &r, &err) == SCHED_E_USAGE);
    o = defaults();
    o.predict = PREDICT_EWMA;
    o.alpha = 1.5;
    CHECK(sim_run(&w, policy_find("sjf"), &params, &o, &r, &err) == SCHED_E_USAGE);
    CHECK_CONTAINS(err.msg, "EWMA");
    o.alpha = 0.5;
    o.tau0 = 0;
    CHECK(sim_run(&w, policy_find("sjf"), &params, &o, &r, &err) == SCHED_E_USAGE);
    o = defaults();
    o.cs_cost = -1;
    CHECK(sim_run(&w, policy_find("fcfs"), &params, &o, &r, &err) == SCHED_E_USAGE);
    CHECK_CONTAINS(err.msg, "context-switch cost");
    o.cs_cost = INT64_MAX / 2;
    CHECK(sim_run(&w, policy_find("fcfs"), &params, &o, &r, &err) == SCHED_E_USAGE);
    workload_free(&w);
}

static void test_segment_limit(void) {
    Triple p[] = {{1, 0, 4}, {2, 0, 4}};
    Workload w = workload_of(p, 2);
    PolicyParams params;
    policy_params_default(&params);
    params.quantum = 1;
    SimOptions o = defaults();
    SimResult r;
    SchedError err;
    /* RR q=1 alternates 8 times; FCFS needs only 2 segments. */
    o.max_segments = 7;
    CHECK(sim_run(&w, policy_find("rr"), &params, &o, &r, &err) == SCHED_E_INPUT);
    CHECK_CONTAINS(err.msg, "rr: schedule needs more than 7 Gantt segments");
    CHECK(r.outcomes == NULL && r.timelines == NULL);
    o.max_segments = 8;
    CHECK(sim_run(&w, policy_find("rr"), &params, &o, &r, &err) == SCHED_OK);
    CHECK_EQ_I64(r.timelines[0].len, 8);
    sim_result_free(&r);
    workload_free(&w);

    Triple gap[] = {{1, 0, 1}, {2, 5, 1}}; /* the idle segment counts too */
    w = workload_of(gap, 2);
    o.max_segments = 2;
    CHECK(sim_run(&w, policy_find("fcfs"), &params, &o, &r, &err) == SCHED_E_INPUT);
    workload_free(&w);
}

static void test_metrics(void) {
    Triple p[] = {{1, 0, 5}, {2, 1, 3}, {3, 2, 8}, {4, 3, 6}, {5, 4, 4}};
    Workload w = workload_of(p, 5);
    SimResult r = run(&w, "fcfs", 2);
    ProcMetrics m = metrics_for(&r, 3); /* P4 runs [16,22) */
    CHECK_EQ_I64(m.pid, 4);
    CHECK_EQ_I64(m.start, 16);
    CHECK_EQ_I64(m.completion, 22);
    CHECK_EQ_I64(m.response, 13);
    CHECK_EQ_I64(m.turnaround, 19);
    CHECK_EQ_I64(m.waiting, 13);
    CHECK(m.slowdown == 19.0 / 6);
    AvgMetrics avg = metrics_averages(&r);
    CHECK(avg.turnaround == 67.0 / 5); /* (5+7+14+19+22)/5 = 13.40 */
    CHECK(avg.waiting == avg.response);
    CHECK_EQ_I64(metrics_makespan(&r), 26);

    Summary s;
    CHECK(metrics_summary(&r, &s) == SCHED_OK);
    CHECK(s.turnaround.mean == 67.0 / 5);
    CHECK(s.turnaround.median == 14);
    CHECK_EQ_I64(s.turnaround.p95, 22); /* rank ceil(0.95 * 5) = 5 */
    CHECK_EQ_I64(s.turnaround.max, 22);
    CHECK(s.waiting.median == 6); /* waits 0 4 6 13 18 */
    CHECK_EQ_I64(s.span, 26);
    CHECK(s.utilization == 1.0);
    CHECK(s.throughput == 5.0 / 26);
    CHECK(s.slowdown_max == 5.5); /* P5: 22 / 4 */
    double sd[] = {1.0, 7.0 / 3, 1.75, 19.0 / 6, 5.5}, sum = 0, sq = 0;
    for (int i = 0; i < 5; i++) sum += sd[i], sq += sd[i] * sd[i];
    CHECK(s.jain_slowdown == sum * sum / (5 * sq));
    CHECK_EQ_I64(s.switches, 4);
    CHECK(s.cs_fraction == 0);
    sim_result_free(&r);
    workload_free(&w);

    /* Even n: median averages the middle pair; p95 of 1..20 is rank 19. */
    Triple twenty[20];
    for (int i = 0; i < 20; i++) twenty[i] = (Triple){i + 1, 0, 1};
    w = workload_of(twenty, 20);
    r = run(&w, "fcfs", 2); /* turnarounds 1..20 */
    CHECK(metrics_summary(&r, &s) == SCHED_OK);
    CHECK(s.turnaround.median == 10.5);
    CHECK_EQ_I64(s.turnaround.p95, 19);
    CHECK_EQ_I64(s.turnaround.p99, 20);
    CHECK(s.jain_slowdown <= 1.0);
    sim_result_free(&r);
    workload_free(&w);
}

static void test_context_switch_cost(void) {
    SimOptions o = defaults();
    o.cs_cost = 1;
    Triple two[] = {{1, 0, 2}, {2, 0, 2}};
    Workload w = workload_of(two, 2);

    SimResult r = run_opts(&w, "fcfs", 2, &o); /* the first dispatch is free */
    const Seg fcfs[] = {{0, 2, 1, false}, {2, 3, 2, true}, {3, 5, 2, false}};
    check_timeline(&r, fcfs, 3);
    CHECK_EQ_I64(r.outcomes[1].start, 3);
    CHECK_EQ_I64(r.switches, 1);
    CHECK_EQ_I64(r.cs_time, 1);
    ProcMetrics m = metrics_for(&r, 1);
    CHECK_EQ_I64(m.waiting, 3); /* ready [0,2) + switch-in [2,3) */
    sim_result_free(&r);

    r = run_opts(&w, "rr", 1, &o);
    const Seg rr[] = {{0, 1, 1, false}, {1, 2, 2, true}, {2, 3, 2, false}, {3, 4, 1, true},
                      {4, 5, 1, false}, {5, 6, 2, true}, {6, 7, 2, false}};
    check_timeline(&r, rr, 7);
    CHECK_EQ_I64(r.switches, 3);
    CHECK_EQ_I64(r.cs_time, 3);
    Summary s;
    CHECK(metrics_summary(&r, &s) == SCHED_OK);
    CHECK(s.cs_fraction == 3.0 / 7);
    CHECK(s.utilization == 4.0 / 7);
    sim_result_free(&r);
    workload_free(&w);

    /* The same process continuing costs nothing. */
    Triple one[] = {{1, 0, 3}};
    w = workload_of(one, 1);
    r = run_opts(&w, "rr", 1, &o);
    CHECK_EQ_I64(r.cs_time, 0);
    sim_result_free(&r);
    workload_free(&w);

    /* SRTF: P3 arrives while P2 is being switched in, so the engine re-decides
     * when the switch ends and switches again to the shorter P3. */
    o.cs_cost = 2;
    Triple three[] = {{1, 0, 10}, {2, 1, 5}, {3, 2, 1}};
    w = workload_of(three, 3);
    r = run_opts(&w, "srtf", 2, &o);
    const Seg srtf[] = {{0, 1, 1, false}, {1, 3, 2, true},   {3, 5, 3, true},   {5, 6, 3, false},
                        {6, 8, 2, true},  {8, 13, 2, false}, {13, 15, 1, true}, {15, 24, 1, false}};
    check_timeline(&r, srtf, 8);
    CHECK_EQ_I64(r.switches, 4);
    CHECK_EQ_I64(r.cs_time, 8);
    CHECK_EQ_I64(r.outcomes[1].start, 8);
    sim_result_free(&r);
    workload_free(&w);
}

static void test_io_bursts(void) {
    const int64_t a[] = {2, 3, 2}, b[] = {4};
    const int64_t *ph[] = {a, b};
    const size_t counts[] = {3, 1};
    const int64_t arr[] = {0, 0};
    Workload w = with_phases(ph, counts, arr, 2);
    SimResult r = run(&w, "fcfs", 2);
    const Seg fcfs[] = {{0, 2, 1, false}, {2, 6, 2, false}, {6, 8, 1, false}};
    check_timeline(&r, fcfs, 3);
    ProcMetrics m = metrics_for(&r, 0);
    CHECK_EQ_I64(m.burst, 4);
    CHECK_EQ_I64(m.io, 3);
    CHECK_EQ_I64(m.turnaround, 8);
    CHECK_EQ_I64(m.waiting, 1); /* ready [5,6) */
    CHECK(m.slowdown == 2.0);
    sim_result_free(&r);
    workload_free(&w);

    /* Alone: the CPU idles while the only process waits for I/O. */
    const int64_t c[] = {1, 4, 1};
    const int64_t *ph1[] = {c};
    const size_t count1[] = {3};
    const int64_t arr1[] = {0};
    w = with_phases(ph1, count1, arr1, 1);
    r = run(&w, "rr", 2);
    const Seg alone[] = {{0, 1, 1, false}, {1, 5, -1, false}, {5, 6, 1, false}};
    check_timeline(&r, alone, 3);
    sim_result_free(&r);
    workload_free(&w);

    /* SRTF: P2 returning from I/O preempts the long P1. */
    const int64_t l[] = {10}, s[] = {1, 2, 1};
    const int64_t *ph2[] = {l, s};
    const size_t count2[] = {1, 3};
    w = with_phases(ph2, count2, arr, 2);
    r = run(&w, "srtf", 2);
    const Seg srtf[] = {{0, 1, 2, false}, {1, 3, 1, false}, {3, 4, 2, false}, {4, 12, 1, false}};
    check_timeline(&r, srtf, 4);
    sim_result_free(&r);
    workload_free(&w);
}

static void test_multicore(void) {
    SimOptions o = defaults();
    o.cores = 2;
    Triple p[] = {{1, 0, 3}, {2, 0, 2}, {3, 0, 4}, {4, 1, 1}};
    Workload w = workload_of(p, 4);
    SimResult r = run_opts(&w, "fcfs", 2, &o);
    CHECK_EQ_I64(r.cores, 2);
    const Seg c0[] = {{0, 3, 1, false}, {3, 4, 4, false}, {4, 6, -1, false}};
    const Seg c1[] = {{0, 2, 2, false}, {2, 6, 3, false}};
    check_core(&r, 0, c0, 3);
    check_core(&r, 1, c1, 2);
    Summary s;
    CHECK(metrics_summary(&r, &s) == SCHED_OK);
    CHECK(s.utilization == 10.0 / 12);
    sim_result_free(&r);
    workload_free(&w);

    /* RR: at t=2 the queue is [P3, P1, P2]; P1 keeps core 0 (affinity) and P3
     * takes core 1. At t=4 P3 keeps core 1 and P2 moves to core 0. */
    Triple q[] = {{1, 0, 4}, {2, 0, 4}, {3, 0, 4}};
    w = workload_of(q, 3);
    r = run_opts(&w, "rr", 2, &o);
    const Seg r0[] = {{0, 4, 1, false}, {4, 6, 2, false}};
    const Seg r1[] = {{0, 2, 2, false}, {2, 6, 3, false}};
    check_core(&r, 0, r0, 2);
    check_core(&r, 1, r1, 2);
    CHECK_EQ_I64(r.switches, 2);
    sim_result_free(&r);
    workload_free(&w);
}

static void test_ewma_changes_decisions(void) {
    /* With tau0 = 5 both jobs look equally long, so SJF falls back to pid. */
    Triple p[] = {{1, 0, 8}, {2, 0, 2}};
    Workload w = workload_of(p, 2);
    SimResult r = run(&w, "sjf", 2);
    const Seg oracle[] = {{0, 2, 2, false}, {2, 10, 1, false}};
    check_timeline(&r, oracle, 2);
    sim_result_free(&r);
    SimOptions o = defaults();
    o.predict = PREDICT_EWMA;
    r = run_opts(&w, "sjf", 2, &o);
    const Seg ewma[] = {{0, 8, 1, false}, {8, 10, 2, false}};
    check_timeline(&r, ewma, 2);
    sim_result_free(&r);
    workload_free(&w);

    /* Bursts 2 then 4, alpha 0.5, tau0 5: predictions 5 and 3.5. */
    const int64_t a[] = {2, 1, 4};
    const int64_t *ph[] = {a};
    const size_t counts[] = {3};
    const int64_t arr[] = {0};
    w = with_phases(ph, counts, arr, 1);
    PredictionError e = metrics_prediction_error(&w, 0.5, 5.0);
    CHECK_EQ_I64(e.bursts, 2);
    CHECK(e.mae == 1.75);
    CHECK(e.mape == 81.25);
    workload_free(&w);
}

/* ---- protocol tests with a recording policy ---- */

typedef struct {
    char log[512];
    size_t len;
    const SchedView *view;
    size_t ready[8];
    size_t nready;
} Recorder;

/* Test knobs, read when a run starts; the log is copied out when it ends. */
static int64_t rec_length = 3;   /* requested slice length */
static int64_t rec_deadline = 0; /* > 0: slices end at now + rec_deadline */
static int64_t rec_hold = 0;     /* stay idle (on purpose) before this time */
static char protocol_log[512];

static void rec_log(Recorder *s, char kind, size_t proc) {
    s->len += (size_t)snprintf(s->log + s->len, sizeof s->log - s->len, "%c%" PRId64 " ", kind,
                               s->view->procs[proc].pid);
}

static void *rec_create(const SchedView *view, const PolicyParams *params, SchedError *err) {
    (void)params;
    (void)err;
    Recorder *s = calloc(1, sizeof *s);
    s->view = view;
    return s;
}
static void rec_destroy(void *self) {
    Recorder *s = self;
    memcpy(protocol_log, s->log, sizeof protocol_log);
    free(s);
}
static void rec_enqueue(Recorder *s, size_t p, char kind) {
    rec_log(s, kind, p);
    s->ready[s->nready++] = p;
}
static void rec_arrival(void *self, size_t p) { rec_enqueue(self, p, 'A'); }
static void rec_ready(void *self, size_t p) { rec_enqueue(self, p, 'W'); }
static void rec_preempt(void *self, size_t p, int64_t ran) {
    (void)ran;
    rec_enqueue(self, p, 'P');
}
static void rec_block(void *self, size_t p, int64_t ran) {
    (void)ran;
    rec_log(self, 'B', p);
}
static void rec_complete(void *self, size_t p, int64_t ran) {
    (void)ran;
    rec_log(self, 'C', p);
}
static bool rec_next(void *self, Slice *out) {
    Recorder *s = self;
    if (s->nready == 0 || s->view->now < rec_hold) return false;
    out->proc = s->ready[0];
    for (size_t i = 1; i < s->nready; i++) s->ready[i - 1] = s->ready[i];
    s->nready--;
    out->length = rec_length;
    out->deadline = rec_deadline > 0 ? s->view->now + rec_deadline : SCHED_NO_DEADLINE;
    rec_log(s, 'R', out->proc);
    return true;
}

static const Policy RECORDER = {.name = "rec",
                                .label = "REC",
                                .heading = "Recorder",
                                .preemptive = true,
                                .work_conserving = false,
                                .create = rec_create,
                                .destroy = rec_destroy,
                                .on_arrival = rec_arrival,
                                .on_ready = rec_ready,
                                .next_slice = rec_next,
                                .on_preempt = rec_preempt,
                                .on_block = rec_block,
                                .on_complete = rec_complete};

static SimResult run_recorder(const Workload *w, int64_t cs_cost) {
    PolicyParams params;
    policy_params_default(&params);
    SimOptions o = defaults();
    o.cs_cost = cs_cost;
    SimResult r;
    if (sim_run(w, &RECORDER, &params, &o, &r, NULL) != SCHED_OK) exit(EXIT_FAILURE);
    return r;
}

static void test_engine_protocol(void) {
    /* P2 and P1 both arrive at 3 (end of P9's first slice); P3 arrives mid-idle. */
    Triple p[] = {{9, 0, 4}, {2, 3, 1}, {1, 3, 1}, {3, 20, 1}};
    Workload w = workload_of(p, 4);
    SimResult r = run_recorder(&w, 0);
    /* Arrivals at a slice boundary are admitted (in pid order) before the preemption is
     * reported; completions are reported after the arrivals at the same instant. */
    CHECK_STR_EQ(protocol_log, "A9 R9 A1 A2 P9 R1 C1 R2 C2 R9 C9 A3 R3 C3 ");
    const Seg exp[] = {{0, 3, 9, false}, {3, 4, 1, false},   {4, 5, 2, false},
                       {5, 6, 9, false}, {6, 20, -1, false}, {20, 21, 3, false}};
    check_timeline(&r, exp, 6);
    sim_result_free(&r);
    workload_free(&w);

    /* I/O: blocking and waking use their own hooks. */
    const int64_t a[] = {1, 2, 1}, b[] = {5};
    const int64_t *ph[] = {a, b};
    const size_t counts[] = {3, 1};
    const int64_t arr[] = {0, 0};
    w = with_phases(ph, counts, arr, 2);
    r = run_recorder(&w, 0);
    CHECK_STR_EQ(protocol_log, "A1 A2 R1 B1 R2 W1 P2 R1 C1 R2 C2 ");
    sim_result_free(&r);
    workload_free(&w);
}

static void test_deadline_and_deliberate_idle(void) {
    /* The policy holds the CPU idle until t=3 although P1 and P2 are ready. */
    Triple p[] = {{1, 0, 2}, {2, 1, 1}, {3, 3, 1}};
    Workload w = workload_of(p, 3);
    rec_hold = 3;
    SimResult r = run_recorder(&w, 0);
    rec_hold = 0;
    CHECK_STR_EQ(protocol_log, "A1 A2 A3 R1 C1 R2 C2 R3 C3 ");
    const Seg held[] = {{0, 3, -1, false}, {3, 5, 1, false}, {5, 6, 2, false}, {6, 7, 3, false}};
    check_timeline(&r, held, 4);
    sim_result_free(&r);
    workload_free(&w);

    /* A deadline ends a long slice early; the engine reports it as a preemption. */
    Triple one[] = {{1, 0, 5}};
    w = workload_of(one, 1);
    rec_length = 100;
    rec_deadline = 2;
    r = run_recorder(&w, 0);
    CHECK_STR_EQ(protocol_log, "A1 R1 P1 R1 P1 R1 C1 ");
    const Seg merged[] = {{0, 5, 1, false}};
    check_timeline(&r, merged, 1);
    sim_result_free(&r);
    workload_free(&w);

    /* A deadline that passes during the switch-in hands the process back unrun. */
    Triple two[] = {{1, 0, 2}, {2, 0, 2}};
    w = workload_of(two, 2);
    r = run_recorder(&w, 3);
    CHECK_STR_EQ(protocol_log, "A1 A2 R1 C1 R2 P2 R2 C2 ");
    const Seg cs[] = {{0, 2, 1, false}, {2, 5, 2, true}, {5, 7, 2, false}};
    check_timeline(&r, cs, 3);
    CHECK_EQ_I64(r.switches, 1);
    CHECK_EQ_I64(r.outcomes[1].start, 5);
    sim_result_free(&r);
    workload_free(&w);
    rec_length = 3;
    rec_deadline = 0;
}

int main(void) {
    RUN_TEST(test_registry);
    RUN_TEST(test_idle_gaps);
    RUN_TEST(test_srtf_preempts_on_arrival);
    RUN_TEST(test_rr_arrival_ordering);
    RUN_TEST(test_rr_coalesces_consecutive_slices);
    RUN_TEST(test_large_values_do_not_overflow);
    RUN_TEST(test_run_errors);
    RUN_TEST(test_segment_limit);
    RUN_TEST(test_metrics);
    RUN_TEST(test_context_switch_cost);
    RUN_TEST(test_io_bursts);
    RUN_TEST(test_multicore);
    RUN_TEST(test_ewma_changes_decisions);
    RUN_TEST(test_engine_protocol);
    RUN_TEST(test_deadline_and_deliberate_idle);
    return CHECK_SUMMARY();
}
