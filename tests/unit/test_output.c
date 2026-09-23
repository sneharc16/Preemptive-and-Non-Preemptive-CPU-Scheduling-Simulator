/* Output writer unit tests: text, CSV and JSON. */
#include "check.h"
#include "sched/io.h"

typedef struct {
    Workload w;
    SimResult r;
    ReportItem item;
} Fixture;

/* P1 arrives at 1, P2 at 5: idle gap [3,5) between them. Fills *f in place:
 * the result borrows f->w, so the fixture must not move. */
static void fixture_opts(Fixture *f, const char *policy, const SimOptions *o) {
    PolicyParams params;
    policy_params_default(&params);
    params.quantum = 1;
    workload_init(&f->w);
    workload_push(&f->w, process_make(1, 1, 2), NULL);
    workload_push(&f->w, process_make(2, 5, 1), NULL);
    if (sim_run(&f->w, policy_find(policy), &params, o, &f->r, NULL) != SCHED_OK) exit(1);
    f->item = (ReportItem){.sim = &f->r};
    if (metrics_summary(&f->r, &f->item.summary) != SCHED_OK) exit(1);
}

static void fixture(Fixture *f, const char *policy) {
    SimOptions o;
    sim_options_default(&o);
    fixture_opts(f, policy, &o);
}

static void fixture_free(Fixture *f) {
    sim_result_free(&f->r);
    workload_free(&f->w);
}

static char *capture_text(const ReportItem *item, TextOptions opt) {
    FILE *out = tmpfile();
    write_text_result(out, item, &opt);
    char *text = stream_contents(out);
    fclose(out);
    return text;
}

static void test_labels(void) {
    char buf[64];
    Fixture f;
    fixture(&f, "rr");
    result_label(&f.r, buf, sizeof buf);
    CHECK_STR_EQ(buf, "RoundRobin(q=1)");
    fixture_free(&f);
    fixture(&f, "srtf");
    result_label(&f.r, buf, sizeof buf);
    CHECK_STR_EQ(buf, "SRTF");
    fixture_free(&f);
}

static void test_text(void) {
    Fixture f;
    fixture(&f, "rr");
    char *text = capture_text(&f.item, (TextOptions){.gantt = true, .per_tick = true});
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

    text = capture_text(&f.item, (TextOptions){.full = true});
    CHECK_STR_EQ(text, "Round Robin Scheduling (q=1) =>\n"
                       "RoundRobin(q=1) Averages:\n  Response:  0.00\n  Waiting :  0.00\n"
                       "  Turnaround:1.50\n\n"
                       "RoundRobin(q=1) Metrics:\n"
                       "  Turnaround: mean 1.50  median 1.50  p95 2  p99 2  max 2\n"
                       "  Waiting:    mean 0.00  median 0.00  p95 0  p99 0  max 0\n"
                       "  Response:   mean 0.00  median 0.00  p95 0  p99 0  max 0\n"
                       "  Slowdown:   mean 1.00  max 1.00  Jain fairness 1.0000\n"
                       "  Throughput: 0.4000 processes/tick  CPU utilization: 60.00%\n"
                       "  Context switches: 1 (0 ticks, 0.00% of CPU time)\n\n");
    free(text);

    f.item.has_gap = true;
    f.item.gap_pct = 12.5;
    f.item.has_regret = true;
    f.item.regret_pct = -3.25;
    text = capture_text(&f.item, (TextOptions){0});
    CHECK_CONTAINS(text, "RoundRobin(q=1) optimality gap: 12.50% above the opt-np mean "
                         "turnaround\n\n");
    CHECK_CONTAINS(text, "RoundRobin(q=1) prediction regret: -3.25% mean turnaround vs oracle\n");
    free(text);
    fixture_free(&f);

    fixture(&f, "fcfs");
    text = capture_text(&f.item, (TextOptions){.gantt = false});
    CHECK_STR_EQ(text,
                 "FCFS (FIFO) Scheduling =>\n"
                 "FCFS Averages:\n  Response:  0.00\n  Waiting :  0.00\n  Turnaround:1.50\n\n");
    free(text);
    fixture_free(&f);
}

static void test_text_switches_and_cores(void) {
    SimOptions o;
    sim_options_default(&o);
    o.cs_cost = 1;
    Fixture f;
    fixture_opts(&f, "fcfs", &o);
    char *text = capture_text(&f.item, (TextOptions){.gantt = true, .per_tick = true});
    CHECK_CONTAINS(text, "[1  ,3  ) P1   | [3  ,5  ) IDLE  | [5  ,6  ) CS    | [6  ,7  ) P2   \n");
    CHECK_CONTAINS(text, "t=5: CS -> P2\nt=6: P2\n");
    free(text);
    fixture_free(&f);

    o.cs_cost = 0;
    o.cores = 2;
    fixture_opts(&f, "fcfs", &o);
    text = capture_text(&f.item, (TextOptions){.gantt = true, .per_tick = true});
    /* P2 has run nowhere yet, so it takes the lowest-numbered free core. */
    CHECK_CONTAINS(text,
                   "Gantt — FCFS (core 0):\n[1  ,3  ) P1   | [3  ,5  ) IDLE  | [5  ,6  ) P2   "
                   "\n\nGantt — FCFS (core 1):\n[1  ,6  ) IDLE  \n\n");
    CHECK_CONTAINS(text, "Per-tick timeline — FCFS (core 1):\nt=1: IDLE\n");
    free(text);
    fixture_free(&f);
}

static void test_text_run(void) {
    Workload w;
    workload_init(&w);
    ReportRun run = {.workload = &w,
                     .has_prediction = true,
                     .prediction = {.bursts = 4, .mae = 1.5, .mape = 20.0},
                     .has_optimum = true,
                     .optimum_mean_turnaround = 7.0};
    FILE *out = tmpfile();
    write_text_run(out, &run);
    char *text = stream_contents(out);
    CHECK_STR_EQ(text, "EWMA burst prediction: MAE 1.5000 ticks, MAPE 20.00% over 4 CPU bursts\n\n"
                       "opt-np optimum mean turnaround: 7.00\n\n");
    free(text);
    fclose(out);
}

static void test_csv(void) {
    Fixture f;
    fixture(&f, "sjf");
    FILE *out = tmpfile();
    write_csv_header(out, false);
    write_csv_rows(out, &f.r, false);
    write_csv_header(out, true);
    write_csv_rows(out, &f.r, true);
    char *text = stream_contents(out);
    CHECK_STR_EQ(text, "Algorithm,PID,Arrival,Burst,Start,Completion,Response,Waiting,Turnaround\n"
                       "SJF,1,1,2,1,3,0,0,2\n"
                       "SJF,2,5,1,5,6,0,0,1\n"
                       "Algorithm,PID,Arrival,Burst,Start,Completion,Response,Waiting,Turnaround,"
                       "IO,Slowdown\n"
                       "SJF,1,1,2,1,3,0,0,2,0,1.000000\n"
                       "SJF,2,5,1,5,6,0,0,1,0,1.000000\n");
    free(text);
    fclose(out);

    out = tmpfile();
    f.item.has_gap = true;
    f.item.gap_pct = 5;
    write_summary_csv(out, &f.item, 1);
    text = stream_contents(out);
    CHECK_STR_EQ(text, "Algorithm,TurnaroundMean,TurnaroundMedian,TurnaroundP95,TurnaroundP99,"
                       "TurnaroundMax,WaitingMean,WaitingMedian,WaitingP95,WaitingP99,WaitingMax,"
                       "ResponseMean,ResponseMedian,ResponseP95,ResponseP99,ResponseMax,"
                       "SlowdownMean,SlowdownMax,JainSlowdown,Throughput,Utilization,"
                       "ContextSwitches,SwitchTime,SwitchFraction,GapPct,RegretPct\n"
                       "SJF,1.500000,1.500000,2,2,2,0.000000,0.000000,0,0,0,0.000000,0.000000,0,0,"
                       "0,1.000000,1.000000,1.000000,0.400000,0.600000,1,0,0.000000,5.000000,\n");
    free(text);
    fclose(out);
    fixture_free(&f);
}

static void test_json(void) {
    Fixture a, b;
    fixture(&a, "fcfs");
    fixture(&b, "rr");
    ReportItem items[] = {a.item, b.item};
    items[1].has_gap = true;
    items[1].gap_pct = 0;
    ReportRun run = {.workload = &a.w, .has_optimum = true, .optimum_mean_turnaround = 1.5};
    FILE *out = tmpfile();
    write_json(out, &run, items, 2);
    char *text = stream_contents(out);
    CHECK_CONTAINS(text, "\"schema_version\": 1");
    CHECK_CONTAINS(text, "{\"pid\": 2, \"arrival\": 5, \"burst\": 1, \"priority\": 0, "
                         "\"tickets\": 100, \"nice\": 0, \"bursts\": [1]}\n  ],");
    CHECK_CONTAINS(text, "\"options\": {\"cores\": 1, \"cs_cost\": 0, \"predict\": \"oracle\"},");
    CHECK_CONTAINS(text, "\"optimum_mean_turnaround\": 1.5,");
    CHECK_CONTAINS(text, "\"algorithm\": \"fcfs\",\n      \"label\": \"FCFS\",\n"
                         "      \"preemptive\": false,\n      \"quantum\": null,");
    CHECK_CONTAINS(text, "\"label\": \"RoundRobin(q=1)\",\n      \"preemptive\": true,\n"
                         "      \"quantum\": 1,");
    CHECK_CONTAINS(text, "{\"pid\": 1, \"arrival\": 1, \"burst\": 2, \"io\": 0, \"start\": 1, "
                         "\"completion\": 3, \"response\": 0, \"waiting\": 0, \"turnaround\": 2, "
                         "\"slowdown\": 1},");
    CHECK_CONTAINS(text, "\"averages\": {\"response\": 0, \"waiting\": 0, \"turnaround\": 1.5}");
    CHECK_CONTAINS(text, "\"turnaround\": {\"mean\": 1.5, \"median\": 1.5, \"p95\": 2, "
                         "\"p99\": 2, \"max\": 2},");
    CHECK_CONTAINS(text, "\"span\": 5, \"throughput\": 0.4, \"cpu_utilization\": 0.6,");
    CHECK_CONTAINS(text, "\"context_switches\": 1, \"switch_time\": 0, \"switch_fraction\": 0\n");
    CHECK_CONTAINS(text, "\"optimality_gap_pct\": 0,\n      \"makespan\": 6,");
    CHECK_CONTAINS(text, "\"makespan\": 6,\n      \"cores\": 1,");
    CHECK_CONTAINS(text, "{\"start\": 3, \"end\": 5, \"pid\": null, \"type\": \"idle\", "
                         "\"core\": 0},\n        {\"start\": 5, \"end\": 6, \"pid\": 2, "
                         "\"type\": \"run\", \"core\": 0}\n      ]");
    free(text);
    fclose(out);
    fixture_free(&a);
    fixture_free(&b);
}

static void test_json_options_and_switches(void) {
    SimOptions o;
    sim_options_default(&o);
    o.cs_cost = 1;
    o.predict = PREDICT_EWMA;
    o.alpha = 0.25;
    Fixture f;
    fixture_opts(&f, "sjf", &o);
    f.item.has_regret = true;
    f.item.regret_pct = 2.5;
    ReportRun run = {.workload = &f.w,
                     .has_prediction = true,
                     .prediction = {.bursts = 2, .mae = 0.5, .mape = 10}};
    FILE *out = tmpfile();
    write_json(out, &run, &f.item, 1);
    char *text = stream_contents(out);
    CHECK_CONTAINS(text, "\"options\": {\"cores\": 1, \"cs_cost\": 1, \"predict\": \"ewma\", "
                         "\"alpha\": 0.25, \"tau0\": 5},");
    CHECK_CONTAINS(text, "\"prediction\": {\"bursts\": 2, \"mae\": 0.5, \"mape_pct\": 10},");
    CHECK_CONTAINS(text, "\"prediction_regret_pct\": 2.5,");
    CHECK_CONTAINS(text, "{\"start\": 5, \"end\": 6, \"pid\": 2, \"type\": \"cs\", \"core\": 0}");
    free(text);
    fclose(out);
    fixture_free(&f);
}

/* Averages that are not short decimals must round-trip exactly. */
static void test_json_double_round_trip(void) {
    Workload w;
    workload_init(&w);
    workload_push(&w, process_make(1, 0, 1), NULL);
    workload_push(&w, process_make(2, 0, 1), NULL);
    workload_push(&w, process_make(3, 0, 2), NULL);
    SimResult r;
    PolicyParams params;
    policy_params_default(&params);
    SimOptions o;
    sim_options_default(&o);
    CHECK(sim_run(&w, policy_find("fcfs"), &params, &o, &r, NULL) == SCHED_OK);
    ReportItem item = {.sim = &r};
    CHECK(metrics_summary(&r, &item.summary) == SCHED_OK);
    ReportRun run = {.workload = &w};
    FILE *out = tmpfile();
    write_json(out, &run, &item, 1);
    char *text = stream_contents(out);
    /* turnaround (1 + 2 + 4) / 3 = 2.333... needs 17 significant digits */
    CHECK_CONTAINS(text, "\"turnaround\": 2.3333333333333335}");
    CHECK_CONTAINS(text, "\"averages\": {\"response\": 1,"); /* (0 + 1 + 2) / 3 */
    free(text);
    fclose(out);
    sim_result_free(&r);
    workload_free(&w);
}

int main(void) {
    RUN_TEST(test_labels);
    RUN_TEST(test_text);
    RUN_TEST(test_text_switches_and_cores);
    RUN_TEST(test_text_run);
    RUN_TEST(test_csv);
    RUN_TEST(test_json);
    RUN_TEST(test_json_options_and_switches);
    RUN_TEST(test_json_double_round_trip);
    return CHECK_SUMMARY();
}
