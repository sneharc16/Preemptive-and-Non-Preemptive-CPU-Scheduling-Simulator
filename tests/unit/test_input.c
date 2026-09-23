/* Input parser unit tests: parse_i64, interactive format, CSV format. */
#include "check.h"
#include "sched/io.h"

static void test_parse_i64(void) {
    int64_t v = 0;
    CHECK(parse_i64("42", &v));
    CHECK_EQ_I64(v, 42);
    CHECK(parse_i64("-7", &v));
    CHECK_EQ_I64(v, -7);
    CHECK(parse_i64("9223372036854775807", &v));
    CHECK_EQ_I64(v, INT64_MAX);
    CHECK(!parse_i64("9223372036854775808", &v));
    CHECK(!parse_i64("", &v));
    CHECK(!parse_i64(" 1", &v));
    CHECK(!parse_i64("1x", &v));
    CHECK(!parse_i64("abc", &v));
    CHECK(!parse_i64("1.5", &v));
}

/* ---- interactive ---- */

static SchedStatus interactive(const char *text, Workload *w, SchedError *err) {
    FILE *in = stream_from(text);
    workload_init(w);
    SchedStatus st = read_workload_interactive(in, NULL, w, err);
    fclose(in);
    return st;
}

static void test_interactive_ok(void) {
    Workload w;
    SchedError err;
    CHECK(interactive("3\n1 0 5\n  2 1\n3\n9\t4 2 trailing-ignored", &w, &err) == SCHED_OK);
    CHECK_EQ_I64(w.len, 3);
    CHECK_EQ_I64(w.items[1].pid, 2);
    CHECK_EQ_I64(w.items[1].burst, 3);
    CHECK_EQ_I64(w.items[2].pid, 9);
    CHECK_EQ_I64(w.items[2].arrival, 4);
    workload_free(&w);
}

static void test_interactive_prompts(void) {
    FILE *in = stream_from("1\n1 0 1\n");
    FILE *out = tmpfile();
    Workload w;
    workload_init(&w);
    CHECK(read_workload_interactive(in, out, &w, NULL) == SCHED_OK);
    char *text = stream_contents(out);
    CHECK_STR_EQ(text, "Number of Processes: Enter details for each process on its own line: "
                       "PID Arrival Burst\n");
    free(text);
    fclose(in);
    fclose(out);
    workload_free(&w);
}

static void check_interactive_error(const char *text, const char *expected) {
    Workload w;
    SchedError err = {{0}};
    CHECK(interactive(text, &w, &err) == SCHED_E_INPUT);
    CHECK_CONTAINS(err.msg, expected);
    workload_free(&w);
}

static void test_interactive_errors(void) {
    check_interactive_error("", "number of processes: unexpected end of input");
    check_interactive_error("0\n", "must be positive (got 0)");
    check_interactive_error("-3\n", "must be positive (got -3)");
    check_interactive_error("two\n", "'two' is not a 64-bit integer");
    check_interactive_error("2\n1 0 5\n", "PID of process #2: unexpected end of input");
    check_interactive_error("1\n1 x 5\n", "arrival of process #1: 'x' is not");
    check_interactive_error("1\n1 0\n", "burst of process #1");
    check_interactive_error("1\n1 0 0\n", "burst of P1 must be > 0");
    check_interactive_error("1\n1 -2 3\n", "arrival of P1 must be >= 0");
    check_interactive_error("1\n-1 0 3\n", "PID must be >= 0");
    check_interactive_error("1\n1 0 99999999999999999999999\n", "not a 64-bit integer");
    check_interactive_error(
        "1\n1 0 1234567890123456789012345678901234567890123456789012345678901234567890\n",
        "token too long");
}

/* ---- CSV ---- */

static SchedStatus csv(const char *text, Workload *w, SchedError *err) {
    FILE *in = stream_from(text);
    workload_init(w);
    SchedStatus st = read_workload_csv(in, w, err);
    fclose(in);
    return st;
}

static void test_csv_ok(void) {
    Workload w;
    SchedError err;
    const char *text = "\xEF\xBB\xBF# a comment\n"
                       "\n"
                       " PID , Burst , arrival \r\n"
                       "1, 5, 0\r\n"
                       "   # indented comment\n"
                       "2,3,1\n"
                       "\n"
                       "3 ,8, 2"; /* no trailing newline */
    CHECK(csv(text, &w, &err) == SCHED_OK);
    CHECK_EQ_I64(w.len, 3);
    CHECK_EQ_I64(w.items[0].pid, 1);
    CHECK_EQ_I64(w.items[0].burst, 5);
    CHECK_EQ_I64(w.items[0].arrival, 0);
    CHECK_EQ_I64(w.items[2].pid, 3);
    CHECK_EQ_I64(w.items[2].burst, 8);
    CHECK_EQ_I64(w.items[2].arrival, 2);
    workload_free(&w);
}

static void check_csv_error(const char *text, const char *expected) {
    Workload w;
    SchedError err = {{0}};
    CHECK(csv(text, &w, &err) == SCHED_E_INPUT);
    CHECK_CONTAINS(err.msg, expected);
    workload_free(&w);
}

static void test_csv_errors(void) {
    check_csv_error("", "input is empty");
    check_csv_error("# only comments\n\n", "input is empty");
    check_csv_error("pid,arrival,burst\n", "no processes listed");
    check_csv_error("1,0,5\n", "line 1: unknown column '1'");
    check_csv_error("pid,arrival,burst,priority\n", "unknown column 'priority'");
    check_csv_error("pid,arrival,pid\n", "column 'pid' appears twice");
    check_csv_error("pid,burst\n1,2\n", "missing required column 'arrival'");
    check_csv_error("pid,arrival,burst\n1,0\n", "line 2: expected 3 fields, found 2");
    check_csv_error("pid,arrival,burst\n1,0,5,6\n", "expected 3 fields, found 4");
    check_csv_error("pid,arrival,burst\n1,zero,5\n", "line 2: column 'arrival' must be");
    check_csv_error("pid,arrival,burst\n1,0,\n",
                    "column 'burst' must be a 64-bit integer (got '')");
    check_csv_error("pid,arrival,burst\n\n1,-1,5\n", "line 3: arrival of P1 must be >= 0");
    check_csv_error("pid,arrival,burst\n1,0,0\n", "burst of P1 must be > 0");
    check_csv_error("a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q\n", "more than 16 fields");

    char long_line[5000];
    memset(long_line, '1', sizeof long_line - 2);
    long_line[sizeof long_line - 2] = '\n';
    long_line[sizeof long_line - 1] = '\0';
    char text[5100] = "pid,arrival,burst\n";
    strcat(text, long_line);
    check_csv_error(text, "line 2: longer than");
}

int main(void) {
    RUN_TEST(test_parse_i64);
    RUN_TEST(test_interactive_ok);
    RUN_TEST(test_interactive_prompts);
    RUN_TEST(test_interactive_errors);
    RUN_TEST(test_csv_ok);
    RUN_TEST(test_csv_errors);
    return CHECK_SUMMARY();
}
