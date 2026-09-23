/* Output writer unit tests: text, CSV and JSON. */
#include "check.h"
#include "sched/io.h"

typedef struct {
    Workload w;
    SimResult r;
    PolicyParams params;
} Fixture;

/* P1 arrives at 1, P2 at 5: idle gap [3,5) between them. Fills *f in place:
 * the result borrows f->w, so the fixture must not move. */
static void fixture(Fixture *f, const char *policy) {
    f->params.quantum = 1;
    workload_init(&f->w);
    workload_push(&f->w, (Process){1, 1, 2}, NULL);
    workload_push(&f->w, (Process){2, 5, 1}, NULL);
    if (sim_run(&f->w, policy_find(policy), &f->params, &f->r, NULL) != SCHED_OK)
        exit(EXIT_FAILURE);
}

static void fixture_free(Fixture *f) {
    sim_result_free(&f->r);
    workload_free(&f->w);
}

static void test_labels(void) {
    char buf[64];
    Fixture f;
    fixture(&f, "rr");
    result_label(&f.r, &f.params, buf, sizeof buf);
    CHECK_STR_EQ(buf, "RoundRobin(q=1)");
    fixture_free(&f);
    fixture(&f, "srtf");
    result_label(&f.r, &f.params, buf, sizeof buf);
    CHECK_STR_EQ(buf, "SRTF");
    fixture_free(&f);
}

static void test_text(void) {
    Fixture f;
    fixture(&f, "rr");
    FILE *out = tmpfile();
    write_text_result(out, &f.r, &f.params, &(TextOptions){.gantt = true, .per_tick = true});
    char *text = stream_contents(out);
    CHECK_STR_EQ(text, "Round Robin Scheduling (q=1) =>\n"
                       "Gantt — RoundRobin(q=1):\n"
                       "[1  ,3  ) P1   | [3  ,5  ) IDLE  | [5  ,6  ) P2   \n"
                       "\n"
                       "Per-tick timeline — RoundRobin(q=1):\n"
                       "t=1: P1\nt=2: P1\nt=3: IDLE\nt=4: IDLE\nt=5: P2\n"
                       "\n"
                       "RoundRobin(q=1) Averages:\n"
                       "  Response:  0.00\n"
                       "  Waiting :  0.00\n"
                       "  Turnaround:1.50\n\n");
    free(text);
    fclose(out);
    fixture_free(&f);

    fixture(&f, "fcfs");
    out = tmpfile();
    write_text_result(out, &f.r, &f.params, &(TextOptions){.gantt = false});
    text = stream_contents(out);
    CHECK_STR_EQ(text,
                 "FCFS (FIFO) Scheduling =>\n"
                 "FCFS Averages:\n  Response:  0.00\n  Waiting :  0.00\n  Turnaround:1.50\n\n");
    free(text);
    fclose(out);
    fixture_free(&f);
}

static void test_csv(void) {
    Fixture f;
    fixture(&f, "sjf");
    FILE *out = tmpfile();
    write_csv_header(out);
    write_csv_rows(out, &f.r, &f.params);
    char *text = stream_contents(out);
    CHECK_STR_EQ(text, "Algorithm,PID,Arrival,Burst,Start,Completion,Response,Waiting,Turnaround\n"
                       "SJF,1,1,2,1,3,0,0,2\n"
                       "SJF,2,5,1,5,6,0,0,1\n");
    free(text);
    fclose(out);
    fixture_free(&f);
}

static void test_json(void) {
    Fixture a, b;
    fixture(&a, "fcfs");
    fixture(&b, "rr");
    SimResult results[] = {a.r, b.r};
    FILE *out = tmpfile();
    write_json(out, &a.w, results, 2, &a.params);
    char *text = stream_contents(out);
    CHECK_CONTAINS(text, "\"schema_version\": 1");
    CHECK_CONTAINS(text, "{\"pid\": 2, \"arrival\": 5, \"burst\": 1}\n  ],");
    CHECK_CONTAINS(text, "\"algorithm\": \"fcfs\",\n      \"label\": \"FCFS\",\n"
                         "      \"preemptive\": false,\n      \"quantum\": null,");
    CHECK_CONTAINS(text, "\"label\": \"RoundRobin(q=1)\",\n      \"preemptive\": true,\n"
                         "      \"quantum\": 1,");
    CHECK_CONTAINS(text, "{\"pid\": 1, \"arrival\": 1, \"burst\": 2, \"start\": 1, "
                         "\"completion\": 3, \"response\": 0, \"waiting\": 0, \"turnaround\": 2},");
    CHECK_CONTAINS(text, "\"averages\": {\"response\": 0, \"waiting\": 0, \"turnaround\": 1.5}");
    CHECK_CONTAINS(text, "\"makespan\": 6,");
    CHECK_CONTAINS(text, "{\"start\": 3, \"end\": 5, \"pid\": null},\n"
                         "        {\"start\": 5, \"end\": 6, \"pid\": 2}\n      ]");
    free(text);
    fclose(out);
    fixture_free(&a);
    fixture_free(&b);
}

/* Averages that are not short decimals must round-trip exactly. */
static void test_json_double_round_trip(void) {
    Workload w;
    workload_init(&w);
    workload_push(&w, (Process){1, 0, 1}, NULL);
    workload_push(&w, (Process){2, 0, 1}, NULL);
    workload_push(&w, (Process){3, 0, 2}, NULL);
    SimResult r;
    PolicyParams params = {.quantum = 1};
    CHECK(sim_run(&w, policy_find("fcfs"), &params, &r, NULL) == SCHED_OK);
    FILE *out = tmpfile();
    write_json(out, &w, &r, 1, &params);
    char *text = stream_contents(out);
    /* turnaround (1 + 2 + 4) / 3 = 2.333... needs 17 significant digits */
    CHECK_CONTAINS(text, "\"turnaround\": 2.3333333333333335}");
    CHECK_CONTAINS(text, "\"response\": 1,"); /* (0 + 1 + 2) / 3 */
    free(text);
    fclose(out);
    sim_result_free(&r);
    workload_free(&w);
}

int main(void) {
    RUN_TEST(test_labels);
    RUN_TEST(test_text);
    RUN_TEST(test_csv);
    RUN_TEST(test_json);
    RUN_TEST(test_json_double_round_trip);
    return CHECK_SUMMARY();
}
