/* Workload container and validation unit tests. */
#include "check.h"
#include "sched/core.h"

static void test_push_grows(void) {
    Workload w;
    workload_init(&w);
    for (int64_t i = 0; i < 1000; i++) {
        CHECK(workload_push(&w, process_make(i, i, 1), NULL) == SCHED_OK);
    }
    CHECK_EQ_I64(w.len, 1000);
    CHECK_EQ_I64(w.items[999].pid, 999);
    CHECK(workload_validate(&w, NULL) == SCHED_OK);
    workload_free(&w);
    CHECK(w.items == NULL && w.len == 0);
}

static void test_empty_rejected(void) {
    Workload w;
    workload_init(&w);
    SchedError err;
    CHECK(workload_validate(&w, &err) == SCHED_E_INPUT);
    CHECK_CONTAINS(err.msg, "empty");
}

static void test_duplicate_pid_rejected(void) {
    Triple p[] = {{4, 0, 1}, {7, 2, 3}, {4, 5, 1}};
    Workload w = workload_of(p, 3);
    SchedError err;
    CHECK(workload_validate(&w, &err) == SCHED_E_INPUT);
    CHECK_CONTAINS(err.msg, "duplicate PID 4");
    workload_free(&w);
}

static SchedStatus validate(Process p, SchedError *err) { return process_validate(&p, 0, err); }

static void test_process_ranges(void) {
    SchedError err;
    CHECK(validate(process_make(-1, 0, 1), &err) == SCHED_E_INPUT);
    CHECK_CONTAINS(err.msg, "PID must be >= 0");
    Process p = process_make(1, -1, 1);
    CHECK(process_validate(&p, 7, &err) == SCHED_E_INPUT);
    CHECK_STR_EQ(err.msg, "line 7: arrival of P1 must be >= 0 (got -1)");
    CHECK(validate(process_make(2, 0, 0), &err) == SCHED_E_INPUT);
    CHECK_STR_EQ(err.msg, "burst of P2 must be > 0 (got 0)");
    CHECK(validate(process_make(0, 0, 1), &err) == SCHED_OK);

    p = process_make(3, 0, 1);
    p.tickets = 0;
    CHECK(validate(p, &err) == SCHED_E_INPUT);
    CHECK_CONTAINS(err.msg, "tickets of P3 must be in 1..1048576 (got 0)");
    p.tickets = SCHED_MAX_TICKETS;
    CHECK(validate(p, &err) == SCHED_OK);
    p.nice = 20;
    CHECK(validate(p, &err) == SCHED_E_INPUT);
    CHECK_CONTAINS(err.msg, "nice of P3 must be in -20..19 (got 20)");
    p.nice = -20;
    p.priority = INT64_MIN;
    CHECK(validate(p, &err) == SCHED_OK);

    Triple bad[] = {{1, 0, 1}, {2, 0, -5}};
    Workload w;
    workload_init(&w);
    CHECK(workload_push(&w, process_make(1, 0, 1), NULL) == SCHED_OK);
    /* workload_push itself refuses non-positive bursts */
    CHECK(workload_push(&w, process_make(bad[1].pid, 0, bad[1].burst), &err) == SCHED_E_INPUT);
    CHECK_CONTAINS(err.msg, "every burst must be > 0");
    workload_free(&w);
}

static void test_phases(void) {
    Workload w;
    workload_init(&w);
    SchedError err;
    int64_t ok[] = {3, 10, 2, 5, 1};
    CHECK(workload_push_phases(&w, process_make(7, 0, 0), ok, 5, &err) == SCHED_OK);
    CHECK_EQ_I64(w.items[0].burst, 6);
    CHECK_EQ_I64(w.items[0].io, 15);
    CHECK_EQ_I64(w.items[0].phase_count, 5);
    CHECK_EQ_I64(workload_phases(&w, 0)[3], 5);
    CHECK(workload_has_io(&w));
    CHECK(workload_push(&w, process_make(8, 1, 4), &err) == SCHED_OK);
    CHECK_EQ_I64(workload_phases(&w, 1)[0], 4);
    CHECK(workload_validate(&w, &err) == SCHED_OK);
    CHECK_EQ_I64(workload_horizon(&w), 1 + 21 + 4);

    int64_t even[] = {3, 10};
    CHECK(workload_push_phases(&w, process_make(9, 0, 0), even, 2, &err) == SCHED_E_INPUT);
    CHECK_CONTAINS(err.msg, "P9: needs an odd number of phases");
    int64_t zero[] = {3, 0, 1};
    CHECK(workload_push_phases(&w, process_make(9, 0, 0), zero, 3, &err) == SCHED_E_INPUT);
    CHECK_CONTAINS(err.msg, "phase 2 is 0");
    int64_t huge[] = {INT64_MAX, 1, 1};
    CHECK(workload_push_phases(&w, process_make(9, 0, 0), huge, 3, &err) == SCHED_E_INPUT);
    CHECK_CONTAINS(err.msg, "overflows");
    CHECK_EQ_I64(w.len, 2);

    /* Tampering with the stored totals is caught by validation. */
    w.items[0].io = 14;
    CHECK(workload_validate(&w, &err) == SCHED_E_INPUT);
    CHECK_CONTAINS(err.msg, "P7: inconsistent burst phases");
    workload_free(&w);

    Workload single;
    workload_init(&single);
    CHECK(workload_push(&single, process_make(1, 0, 2), NULL) == SCHED_OK);
    CHECK(!workload_has_io(&single));
    workload_free(&single);
}

static void test_overflow_rejected(void) {
    SchedError err;
    Triple sum[] = {{1, 0, INT64_MAX}, {2, 0, 1}};
    Workload w = workload_of(sum, 2);
    CHECK(workload_validate(&w, &err) == SCHED_E_INPUT);
    CHECK_CONTAINS(err.msg, "sum of burst times overflows");
    workload_free(&w);

    Triple horizon[] = {{1, INT64_MAX - 5, 3}, {2, 0, 3}};
    w = workload_of(horizon, 2);
    CHECK(workload_validate(&w, &err) == SCHED_E_INPUT);
    CHECK_CONTAINS(err.msg, "latest arrival + total burst");
    workload_free(&w);

    /* Exactly at the limit is fine. */
    Triple edge[] = {{1, INT64_MAX - 6, 3}, {2, 0, 3}};
    w = workload_of(edge, 2);
    CHECK(workload_validate(&w, &err) == SCHED_OK);
    workload_free(&w);
}

static void test_error_set_null_is_noop(void) {
    sched_error_set(NULL, "ignored %d", 1);
    SchedError err;
    sched_error_set(&err, "value %d", 42);
    CHECK_STR_EQ(err.msg, "value 42");
}

int main(void) {
    RUN_TEST(test_push_grows);
    RUN_TEST(test_empty_rejected);
    RUN_TEST(test_duplicate_pid_rejected);
    RUN_TEST(test_process_ranges);
    RUN_TEST(test_phases);
    RUN_TEST(test_overflow_rejected);
    RUN_TEST(test_error_set_null_is_noop);
    return CHECK_SUMMARY();
}
