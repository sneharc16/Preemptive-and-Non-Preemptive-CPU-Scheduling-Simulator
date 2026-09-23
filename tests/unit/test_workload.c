/* Workload container and validation unit tests. */
#include "check.h"
#include "sched/core.h"

static Workload make(const Process *procs, size_t n) {
    Workload w;
    workload_init(&w);
    for (size_t i = 0; i < n; i++) {
        if (workload_push(&w, procs[i], NULL) != SCHED_OK) exit(EXIT_FAILURE);
    }
    return w;
}

static void test_push_grows(void) {
    Workload w;
    workload_init(&w);
    for (int64_t i = 0; i < 1000; i++) {
        CHECK(workload_push(&w, (Process){i, i, 1}, NULL) == SCHED_OK);
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
    Process p[] = {{4, 0, 1}, {7, 2, 3}, {4, 5, 1}};
    Workload w = make(p, 3);
    SchedError err;
    CHECK(workload_validate(&w, &err) == SCHED_E_INPUT);
    CHECK_CONTAINS(err.msg, "duplicate PID 4");
    workload_free(&w);
}

static void test_process_ranges(void) {
    SchedError err;
    CHECK(process_validate(&(Process){-1, 0, 1}, 0, &err) == SCHED_E_INPUT);
    CHECK_CONTAINS(err.msg, "PID must be >= 0");
    CHECK(process_validate(&(Process){1, -1, 1}, 7, &err) == SCHED_E_INPUT);
    CHECK_STR_EQ(err.msg, "line 7: arrival of P1 must be >= 0 (got -1)");
    CHECK(process_validate(&(Process){2, 0, 0}, 0, &err) == SCHED_E_INPUT);
    CHECK_STR_EQ(err.msg, "burst of P2 must be > 0 (got 0)");
    CHECK(process_validate(&(Process){0, 0, 1}, 0, &err) == SCHED_OK);

    Process bad[] = {{1, 0, 1}, {2, 0, -5}};
    Workload w = make(bad, 2);
    CHECK(workload_validate(&w, &err) == SCHED_E_INPUT);
    CHECK_CONTAINS(err.msg, "burst of P2");
    workload_free(&w);
}

static void test_overflow_rejected(void) {
    SchedError err;
    Process sum[] = {{1, 0, INT64_MAX}, {2, 0, 1}};
    Workload w = make(sum, 2);
    CHECK(workload_validate(&w, &err) == SCHED_E_INPUT);
    CHECK_CONTAINS(err.msg, "sum of burst times overflows");
    workload_free(&w);

    Process horizon[] = {{1, INT64_MAX - 5, 3}, {2, 0, 3}};
    w = make(horizon, 2);
    CHECK(workload_validate(&w, &err) == SCHED_E_INPUT);
    CHECK_CONTAINS(err.msg, "latest arrival + total burst");
    workload_free(&w);

    /* Exactly at the limit is fine. */
    Process edge[] = {{1, INT64_MAX - 6, 3}, {2, 0, 3}};
    w = make(edge, 2);
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
    RUN_TEST(test_overflow_rejected);
    RUN_TEST(test_error_set_null_is_noop);
    return CHECK_SUMMARY();
}
