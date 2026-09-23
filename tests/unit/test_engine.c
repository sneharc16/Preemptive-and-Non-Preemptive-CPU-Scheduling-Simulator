/* Engine, registry and metrics unit tests. */
#include "check.h"
#include "sched/engine.h"
#include "sched/metrics.h"

static Workload make(const Process *procs, size_t n) {
    Workload w;
    workload_init(&w);
    for (size_t i = 0; i < n; i++) {
        if (workload_push(&w, procs[i], NULL) != SCHED_OK) exit(EXIT_FAILURE);
    }
    return w;
}

static SimResult run(const Workload *w, const char *name, int64_t quantum) {
    SimResult r;
    PolicyParams params = {.quantum = quantum};
    SchedError err;
    if (sim_run(w, policy_find(name), &params, &r, &err) != SCHED_OK) {
        fprintf(stderr, "sim_run(%s) failed: %s\n", name, err.msg);
        exit(EXIT_FAILURE);
    }
    return r;
}

/* Expected segment: pid < 0 means idle. */
typedef struct {
    int64_t start, end, pid;
} Seg;

static void check_timeline(const SimResult *r, const Seg *exp, size_t n) {
    CHECK_EQ_I64(r->timeline.len, n);
    for (size_t i = 0; i < n && i < r->timeline.len; i++) {
        const Segment *s = &r->timeline.items[i];
        CHECK_EQ_I64(s->start, exp[i].start);
        CHECK_EQ_I64(s->end, exp[i].end);
        if (exp[i].pid < 0) {
            CHECK(s->proc == SCHED_IDLE);
        } else {
            CHECK(s->proc != SCHED_IDLE);
            if (s->proc != SCHED_IDLE) CHECK_EQ_I64(r->workload->items[s->proc].pid, exp[i].pid);
        }
    }
}

static void test_registry(void) {
    CHECK_EQ_I64(policy_count(), 4);
    CHECK_STR_EQ(policy_at(0)->name, "fcfs");
    CHECK_STR_EQ(policy_at(3)->name, "rr");
    CHECK(policy_at(4) == NULL);
    CHECK(policy_find("srtf") == policy_at(2));
    CHECK(policy_find("SRTF") == NULL);
    CHECK(policy_find("lottery") == NULL);
    for (size_t i = 0; i < policy_count(); i++) {
        const Policy *p = policy_at(i);
        CHECK(p->create && p->destroy && p->on_arrival && p->next_slice);
        CHECK(!p->preemptive == !p->on_preempt); /* preemptive policies must requeue */
    }
}

/* Idle gap at the start and in the middle; timeline begins at first arrival. */
static void test_idle_gaps(void) {
    Process p[] = {{1, 2, 3}, {2, 7, 2}, {3, 8, 1}};
    Workload w = make(p, 3);
    const char *names[] = {"fcfs", "sjf", "srtf", "rr"};
    const Seg exp[] = {{2, 5, 1}, {5, 7, -1}, {7, 9, 2}, {9, 10, 3}};
    for (size_t i = 0; i < 4; i++) {
        SimResult r = run(&w, names[i], 2);
        check_timeline(&r, exp, 4);
        CHECK_EQ_I64(metrics_makespan(&r), 10);
        sim_result_free(&r);
    }
    workload_free(&w);
}

static void test_srtf_preempts_on_arrival(void) {
    Process p[] = {{1, 0, 8}, {2, 3, 2}, {3, 4, 5}};
    Workload w = make(p, 3);
    SimResult r = run(&w, "srtf", 2);
    const Seg exp[] = {{0, 3, 1}, {3, 5, 2}, {5, 10, 1}, {10, 15, 3}};
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
    Process p[] = {{1, 0, 4}, {2, 2, 2}, {3, 1, 1}};
    Workload w = make(p, 3);
    SimResult r = run(&w, "rr", 2);
    const Seg exp[] = {{0, 2, 1}, {2, 3, 3}, {3, 5, 2}, {5, 7, 1}};
    check_timeline(&r, exp, 4);
    sim_result_free(&r);
    workload_free(&w);
}

static void test_rr_coalesces_consecutive_slices(void) {
    Process p[] = {{5, 0, 7}};
    Workload w = make(p, 1);
    SimResult r = run(&w, "rr", 1);
    const Seg exp[] = {{0, 7, 5}};
    check_timeline(&r, exp, 1);
    sim_result_free(&r);
    workload_free(&w);
}

static void test_large_values_do_not_overflow(void) {
    Process p[] = {{1, 0, 2000000000}, {2, 0, 2000000000}};
    Workload w = make(p, 2);
    SimResult r = run(&w, "fcfs", 2);
    CHECK_EQ_I64(r.outcomes[1].completion, 4000000000);
    sim_result_free(&r);

    r = run(&w, "rr", 1000000000);
    CHECK_EQ_I64(r.outcomes[0].completion, 3000000000);
    CHECK_EQ_I64(r.outcomes[1].completion, 4000000000);
    sim_result_free(&r);
    workload_free(&w);

    Process edge[] = {{1, INT64_MAX - 10, 4}, {2, 0, 6}};
    w = make(edge, 2);
    r = run(&w, "srtf", 1);
    CHECK_EQ_I64(r.outcomes[1].completion, 6);
    CHECK_EQ_I64(r.outcomes[0].completion, INT64_MAX - 6);
    sim_result_free(&r);
    workload_free(&w);
}

static void test_run_errors(void) {
    SimResult r;
    SchedError err;
    PolicyParams params = {.quantum = 2};
    Process dup[] = {{1, 0, 1}, {1, 1, 1}};
    Workload w = make(dup, 2);
    CHECK(sim_run(&w, policy_find("fcfs"), &params, &r, &err) == SCHED_E_INPUT);
    CHECK_CONTAINS(err.msg, "duplicate PID 1");
    CHECK(r.outcomes == NULL && r.timeline.items == NULL);
    workload_free(&w);

    Process ok[] = {{1, 0, 1}};
    w = make(ok, 1);
    params.quantum = 0;
    CHECK(sim_run(&w, policy_find("rr"), &params, &r, &err) == SCHED_E_USAGE);
    CHECK_CONTAINS(err.msg, "quantum");
    CHECK(sim_run(&w, policy_find("fcfs"), &params, &r, &err) == SCHED_OK); /* quantum unused */
    sim_result_free(&r);
    workload_free(&w);
}

static void test_segment_limit(void) {
    Process p[] = {{1, 0, 4}, {2, 0, 4}};
    Workload w = make(p, 2);
    PolicyParams params = {.quantum = 1};
    SimResult r;
    SchedError err;
    /* RR q=1 alternates 8 times; FCFS needs only 2 segments. */
    CHECK(sim_run_limited(&w, policy_find("rr"), &params, &(SimLimits){.max_segments = 7}, &r,
                          &err) == SCHED_E_INPUT);
    CHECK_CONTAINS(err.msg, "rr: schedule needs more than 7 Gantt segments");
    CHECK(r.outcomes == NULL && r.timeline.items == NULL);
    CHECK(sim_run_limited(&w, policy_find("rr"), &params, &(SimLimits){.max_segments = 8}, &r,
                          &err) == SCHED_OK);
    CHECK_EQ_I64(r.timeline.len, 8);
    sim_result_free(&r);

    Process gap[] = {{1, 0, 1}, {2, 5, 1}}; /* the idle segment counts too */
    workload_free(&w);
    w = make(gap, 2);
    CHECK(sim_run_limited(&w, policy_find("fcfs"), &params, &(SimLimits){.max_segments = 1}, &r,
                          &err) == SCHED_E_INPUT);
    workload_free(&w);
}

static void test_metrics(void) {
    Process p[] = {{1, 0, 5}, {2, 1, 3}, {3, 2, 8}, {4, 3, 6}, {5, 4, 4}};
    Workload w = make(p, 5);
    SimResult r = run(&w, "fcfs", 2);
    ProcMetrics m = metrics_for(&r, 3); /* P4 runs [16,22) */
    CHECK_EQ_I64(m.pid, 4);
    CHECK_EQ_I64(m.start, 16);
    CHECK_EQ_I64(m.completion, 22);
    CHECK_EQ_I64(m.response, 13);
    CHECK_EQ_I64(m.turnaround, 19);
    CHECK_EQ_I64(m.waiting, 13);
    AvgMetrics avg = metrics_averages(&r);
    CHECK(avg.turnaround == 67.0 / 5); /* (5+7+14+19+22)/5 = 13.40 */
    CHECK(avg.waiting == avg.response);
    CHECK_EQ_I64(metrics_makespan(&r), 26);
    sim_result_free(&r);
    workload_free(&w);
}

/* ---- protocol test with a recording policy ---- */

typedef struct {
    char log[512];
    size_t len;
    const SchedView *view;
    size_t ready[8];
    size_t nready;
} Recorder;

static void rec_log(Recorder *s, char kind, size_t proc) {
    s->len += (size_t)snprintf(s->log + s->len, sizeof s->log - s->len, "%c%" PRId64 " ", kind,
                               s->view->procs[proc].pid);
}

static void *rec_create(const SchedView *view, const PolicyParams *params) {
    (void)params;
    Recorder *s = calloc(1, sizeof *s);
    s->view = view;
    return s;
}
/* The recorder's state dies with the run, so it leaves its log here. */
static char protocol_log[512];
static void rec_destroy(void *self) {
    Recorder *s = self;
    memcpy(protocol_log, s->log, sizeof protocol_log);
    free(s);
}
static void rec_arrival(void *self, size_t p) {
    Recorder *s = self;
    rec_log(s, 'A', p);
    s->ready[s->nready++] = p;
}
static void rec_preempt(void *self, size_t p) {
    Recorder *s = self;
    rec_log(s, 'P', p);
    s->ready[s->nready++] = p;
}
static void rec_complete(void *self, size_t p) { rec_log(self, 'C', p); }
static bool rec_next(void *self, Slice *out) {
    Recorder *s = self;
    if (s->nready == 0) return false;
    out->proc = s->ready[0];
    for (size_t i = 1; i < s->nready; i++) s->ready[i - 1] = s->ready[i];
    s->nready--;
    out->length = 3;
    rec_log(s, 'R', out->proc);
    return true;
}

static void test_engine_protocol(void) {
    const Policy rec = {.name = "rec",
                        .label = "REC",
                        .heading = "Recorder",
                        .preemptive = true,
                        .create = rec_create,
                        .destroy = rec_destroy,
                        .on_arrival = rec_arrival,
                        .next_slice = rec_next,
                        .on_preempt = rec_preempt,
                        .on_complete = rec_complete};
    /* P2 and P1 both arrive at 3 (end of P9's first slice); P3 arrives mid-idle. */
    Process p[] = {{9, 0, 4}, {2, 3, 1}, {1, 3, 1}, {3, 20, 1}};
    Workload w = make(p, 4);
    SimResult r;
    PolicyParams params = {.quantum = 1};
    CHECK(sim_run(&w, &rec, &params, &r, NULL) == SCHED_OK);
    /* Arrivals at a slice boundary are admitted (in pid order) before the preemption is
     * reported; completions are reported after the arrivals at the same instant. */
    CHECK_STR_EQ(protocol_log, "A9 R9 A1 A2 P9 R1 C1 R2 C2 R9 C9 A3 R3 C3 ");
    const Seg exp[] = {{0, 3, 9}, {3, 4, 1}, {4, 5, 2}, {5, 6, 9}, {6, 20, -1}, {20, 21, 3}};
    check_timeline(&r, exp, 6);
    sim_result_free(&r);
    workload_free(&w);
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
    RUN_TEST(test_engine_protocol);
    return CHECK_SUMMARY();
}
