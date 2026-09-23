/* sched: command-line front end for the scheduling simulator. */
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sched/engine.h"
#include "sched/io.h"
#include "sched/metrics.h"
#include "sched/policy.h"

#define DEFAULT_CSV_PATH "schedule_metrics.csv"

/* The four original policies, run when --algo is not given. */
static const char *const CLASSIC[] = {"fcfs", "sjf", "srtf", "rr"};

typedef enum { FORMAT_TEXT, FORMAT_CSV, FORMAT_JSON, FORMAT_NONE } Format;
typedef enum { CSV_FILE_DEFAULT, CSV_FILE_EXPLICIT, CSV_FILE_OFF } CsvFileMode;

typedef struct {
    bool selected[SCHED_MAX_POLICIES];
    PolicyParams params;
    SimOptions sim;
    bool quanta_given, levels_given, allot_given;
    size_t n_quanta, n_allot;
    const char *input_path; /* NULL: interactive format on stdin */
    Format format;
    CsvFileMode csv_mode;
    const char *csv_path;
    const char *summary_csv_path;
    bool gantt;
    bool per_tick;
    bool full_metrics;
    bool gap;
    bool timing;
    bool help;
} Options;

static void print_usage(FILE *out, const char *prog) {
    fprintf(out,
            "Usage: %s [options]\n"
            "Simulates CPU scheduling policies on a workload read from stdin (interactive\n"
            "format: N, then N lines of 'PID Arrival Burst') or from --input=FILE.\n"
            "\n"
            "Policies:\n"
            "  --algo=LIST       comma-separated policies; 'classic' (default: fcfs,sjf,srtf,rr)\n"
            "                    or 'all' (every online policy). Available:",
            prog);
    for (size_t i = 0; i < policy_count(); i++) fprintf(out, " %s", policy_at(i)->name);
    fputs("\n"
          "  --quantum=Q       time quantum for rr, lottery, stride (default 2)\n"
          "  --aging=K         prio/prio-p: priority improves by 1 per K ticks waiting (0 = off)\n"
          "  --mlfq-levels=L   MLFQ queue levels (default 3)\n"
          "  --mlfq-quanta=LIST  per-level time slices (default 2,4,8,...)\n"
          "  --mlfq-allot=LIST per-level time allotments before demotion (default = quanta)\n"
          "  --mlfq-boost=S    move every job to the top queue every S ticks (0 = off)\n"
          "  --seed=N          lottery random seed (default 1)\n"
          "  --cfs-latency=L   CFS target latency in ticks (default 24)\n"
          "  --cfs-min-gran=G  CFS minimum granularity in ticks (default 3)\n"
          "\n"
          "Machine model:\n"
          "  --cores=N         number of CPUs sharing one ready queue (default 1)\n"
          "  --cs-cost=C       ticks per context switch between different processes (default 0)\n"
          "  --predict=MODE    burst knowledge for sjf/srtf/hrrn: oracle (default) or ewma\n"
          "  --alpha=A         EWMA weight of the latest burst, 0..1 (default 0.5)\n"
          "  --tau0=T          EWMA initial prediction, > 0 (default 5)\n"
          "\n"
          "Input and output:\n"
          "  --input=FILE      read a CSV workload (columns pid,arrival,burst|bursts and\n"
          "                    optional priority,tickets,nice)\n"
          "  --format=FMT      stdout format: text (default), csv, json, or none (benchmarks)\n"
          "  --timing          print each policy's simulation wall time to stderr\n"
          "  --metrics=M       basic (default) or full: percentiles, slowdown, fairness, ...\n"
          "  --gap             report each policy's optimality gap against opt-np\n"
          "  --csv=FILE        also write per-process metrics as CSV to FILE; in text format\n"
          "                    " DEFAULT_CSV_PATH " is written by default\n"
          "  --no-csv          do not write a per-process CSV file\n"
          "  --summary-csv=FILE  write one row of aggregate metrics per policy to FILE\n"
          "  --no-gantt        omit Gantt charts from text output\n"
          "  --per-tick        add a per-tick timeline to text output\n"
          "  -h, --help        show this help and exit\n"
          "\n"
          "Exit status: 0 success, 1 invalid input, 2 invalid usage, 3 I/O error,\n"
          "4 out of memory.\n",
          out);
}

static const char *flag_value(const char *arg, const char *flag) {
    size_t len = strlen(flag);
    return strncmp(arg, flag, len) == 0 ? arg + len : NULL;
}

static void select_named(Options *opt, const char *name) {
    for (size_t i = 0; i < policy_count(); i++) {
        if (strcmp(policy_at(i)->name, name) == 0) opt->selected[i] = true;
    }
}

static SchedStatus parse_algos(Options *opt, const char *list, SchedError *err) {
    memset(opt->selected, 0, sizeof opt->selected);
    const char *p = list;
    for (;;) {
        size_t len = strcspn(p, ",");
        bool found = false;
        if (len == 3 && strncmp(p, "all", 3) == 0) {
            for (size_t i = 0; i < policy_count(); i++) {
                if (!policy_at(i)->offline) opt->selected[i] = true;
            }
            found = true;
        } else if (len == 7 && strncmp(p, "classic", 7) == 0) {
            for (size_t i = 0; i < sizeof CLASSIC / sizeof CLASSIC[0]; i++) {
                select_named(opt, CLASSIC[i]);
            }
            found = true;
        }
        for (size_t i = 0; i < policy_count() && !found; i++) {
            const char *name = policy_at(i)->name;
            if (strlen(name) == len && strncmp(name, p, len) == 0) {
                opt->selected[i] = found = true;
            }
        }
        if (!found) {
            sched_error_set(err, "unknown algorithm '%.*s' in --algo (see --help)", (int)len, p);
            return SCHED_E_USAGE;
        }
        if (p[len] == '\0') return SCHED_OK;
        p += len + 1;
    }
}

static bool parse_positive(const char *v, int64_t *out) { return parse_i64(v, out) && *out > 0; }
static bool parse_nonneg(const char *v, int64_t *out) { return parse_i64(v, out) && *out >= 0; }

static bool parse_double(const char *v, double *out) {
    if (!*v) return false;
    char *end;
    errno = 0;
    *out = strtod(v, &end);
    return errno == 0 && *end == '\0' && isfinite(*out);
}

/* Comma-separated positive integers into out[0..max); returns the count or 0. */
static size_t parse_list(const char *v, int64_t *out, size_t max) {
    size_t count = 0;
    char buf[32];
    for (;;) {
        size_t len = strcspn(v, ",");
        if (count == max || len == 0 || len >= sizeof buf) return 0;
        memcpy(buf, v, len);
        buf[len] = '\0';
        if (!parse_positive(buf, &out[count++])) return 0;
        if (v[len] == '\0') return count;
        v += len + 1;
    }
}

#define USAGE_ERROR(...)                                                                           \
    do {                                                                                           \
        sched_error_set(err, __VA_ARGS__);                                                         \
        return SCHED_E_USAGE;                                                                      \
    } while (0)

static SchedStatus parse_option(Options *opt, const char *arg, SchedError *err) {
    const char *v;
    PolicyParams *pp = &opt->params;
    if ((v = flag_value(arg, "--algo="))) return parse_algos(opt, v, err);
    if ((v = flag_value(arg, "--quantum="))) {
        if (!parse_positive(v, &pp->quantum))
            USAGE_ERROR("--quantum must be an integer > 0 (got '%s')", v);
    } else if ((v = flag_value(arg, "--aging="))) {
        if (!parse_nonneg(v, &pp->aging))
            USAGE_ERROR("--aging must be an integer >= 0 (got '%s')", v);
    } else if ((v = flag_value(arg, "--mlfq-levels="))) {
        int64_t l;
        if (!parse_positive(v, &l) || l > SCHED_MLFQ_MAX_LEVELS)
            USAGE_ERROR("--mlfq-levels must be in 1..%d (got '%s')", SCHED_MLFQ_MAX_LEVELS, v);
        pp->mlfq_levels = (size_t)l;
        opt->levels_given = true;
    } else if ((v = flag_value(arg, "--mlfq-quanta="))) {
        if (!(opt->n_quanta = parse_list(v, pp->mlfq_quanta, SCHED_MLFQ_MAX_LEVELS)))
            USAGE_ERROR("--mlfq-quanta must be 1..%d comma-separated integers > 0 (got '%s')",
                        SCHED_MLFQ_MAX_LEVELS, v);
        opt->quanta_given = true;
    } else if ((v = flag_value(arg, "--mlfq-allot="))) {
        if (!(opt->n_allot = parse_list(v, pp->mlfq_allotment, SCHED_MLFQ_MAX_LEVELS)))
            USAGE_ERROR("--mlfq-allot must be 1..%d comma-separated integers > 0 (got '%s')",
                        SCHED_MLFQ_MAX_LEVELS, v);
        opt->allot_given = true;
    } else if ((v = flag_value(arg, "--mlfq-boost="))) {
        if (!parse_nonneg(v, &pp->mlfq_boost))
            USAGE_ERROR("--mlfq-boost must be an integer >= 0 (got '%s')", v);
    } else if ((v = flag_value(arg, "--seed="))) {
        int64_t seed;
        if (!parse_nonneg(v, &seed)) USAGE_ERROR("--seed must be an integer >= 0 (got '%s')", v);
        pp->seed = (uint64_t)seed;
    } else if ((v = flag_value(arg, "--cfs-latency="))) {
        if (!parse_positive(v, &pp->cfs_latency))
            USAGE_ERROR("--cfs-latency must be an integer > 0 (got '%s')", v);
    } else if ((v = flag_value(arg, "--cfs-min-gran="))) {
        if (!parse_positive(v, &pp->cfs_min_gran))
            USAGE_ERROR("--cfs-min-gran must be an integer > 0 (got '%s')", v);
    } else if ((v = flag_value(arg, "--cores="))) {
        int64_t c;
        if (!parse_positive(v, &c) || c > SCHED_MAX_CORES)
            USAGE_ERROR("--cores must be in 1..%d (got '%s')", SCHED_MAX_CORES, v);
        opt->sim.cores = (size_t)c;
    } else if ((v = flag_value(arg, "--cs-cost="))) {
        if (!parse_nonneg(v, &opt->sim.cs_cost))
            USAGE_ERROR("--cs-cost must be an integer >= 0 (got '%s')", v);
    } else if ((v = flag_value(arg, "--predict="))) {
        if (strcmp(v, "oracle") == 0) {
            opt->sim.predict = PREDICT_ORACLE;
        } else if (strcmp(v, "ewma") == 0) {
            opt->sim.predict = PREDICT_EWMA;
        } else {
            USAGE_ERROR("--predict must be oracle or ewma (got '%s')", v);
        }
    } else if ((v = flag_value(arg, "--alpha="))) {
        if (!parse_double(v, &opt->sim.alpha) || opt->sim.alpha < 0 || opt->sim.alpha > 1)
            USAGE_ERROR("--alpha must be a number in [0, 1] (got '%s')", v);
    } else if ((v = flag_value(arg, "--tau0="))) {
        if (!parse_double(v, &opt->sim.tau0) || opt->sim.tau0 <= 0)
            USAGE_ERROR("--tau0 must be a number > 0 (got '%s')", v);
    } else if ((v = flag_value(arg, "--input="))) {
        if (!*v) USAGE_ERROR("--input needs a file name");
        opt->input_path = v;
    } else if ((v = flag_value(arg, "--format="))) {
        if (strcmp(v, "text") == 0) {
            opt->format = FORMAT_TEXT;
        } else if (strcmp(v, "csv") == 0) {
            opt->format = FORMAT_CSV;
        } else if (strcmp(v, "json") == 0) {
            opt->format = FORMAT_JSON;
        } else if (strcmp(v, "none") == 0) {
            opt->format = FORMAT_NONE;
        } else {
            USAGE_ERROR("--format must be text, csv, json or none (got '%s')", v);
        }
    } else if ((v = flag_value(arg, "--metrics="))) {
        if (strcmp(v, "basic") == 0) {
            opt->full_metrics = false;
        } else if (strcmp(v, "full") == 0) {
            opt->full_metrics = true;
        } else {
            USAGE_ERROR("--metrics must be basic or full (got '%s')", v);
        }
    } else if ((v = flag_value(arg, "--csv="))) {
        if (!*v) USAGE_ERROR("--csv needs a file name");
        opt->csv_mode = CSV_FILE_EXPLICIT;
        opt->csv_path = v;
    } else if ((v = flag_value(arg, "--summary-csv="))) {
        if (!*v) USAGE_ERROR("--summary-csv needs a file name");
        opt->summary_csv_path = v;
    } else if (strcmp(arg, "--timing") == 0) {
        opt->timing = true;
    } else if (strcmp(arg, "--gap") == 0) {
        opt->gap = true;
    } else if (strcmp(arg, "--no-csv") == 0) {
        opt->csv_mode = CSV_FILE_OFF;
    } else if (strcmp(arg, "--no-gantt") == 0) {
        opt->gantt = false;
    } else if (strcmp(arg, "--per-tick") == 0) {
        opt->per_tick = true;
    } else if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
        opt->help = true;
    } else {
        USAGE_ERROR("unknown option '%s' (see --help)", arg);
    }
    return SCHED_OK;
}

/* Reconciles --mlfq-levels, --mlfq-quanta and --mlfq-allot. */
static SchedStatus finish_mlfq(Options *opt, SchedError *err) {
    PolicyParams *pp = &opt->params;
    if (opt->quanta_given) {
        if (opt->levels_given && opt->n_quanta != pp->mlfq_levels)
            USAGE_ERROR("--mlfq-quanta lists %zu values but --mlfq-levels is %zu", opt->n_quanta,
                        pp->mlfq_levels);
        pp->mlfq_levels = opt->n_quanta;
    }
    if (opt->allot_given) {
        if (opt->n_allot != pp->mlfq_levels)
            USAGE_ERROR("--mlfq-allot lists %zu values but MLFQ has %zu levels", opt->n_allot,
                        pp->mlfq_levels);
    } else {
        memcpy(pp->mlfq_allotment, pp->mlfq_quanta, sizeof pp->mlfq_allotment);
    }
    return SCHED_OK;
}

static SchedStatus parse_options(int argc, char **argv, Options *opt, SchedError *err) {
    *opt = (Options){.format = FORMAT_TEXT,
                     .csv_mode = CSV_FILE_DEFAULT,
                     .csv_path = DEFAULT_CSV_PATH,
                     .gantt = true};
    policy_params_default(&opt->params);
    sim_options_default(&opt->sim);
    for (size_t i = 0; i < sizeof CLASSIC / sizeof CLASSIC[0]; i++) select_named(opt, CLASSIC[i]);

    for (int i = 1; i < argc; i++) {
        SchedStatus st = parse_option(opt, argv[i], err);
        if (st != SCHED_OK) return st;
    }
    return finish_mlfq(opt, err);
}

static SchedStatus load_workload(const Options *opt, Workload *w, SchedError *err) {
    SchedStatus st;
    if (opt->input_path) {
        FILE *in = fopen(opt->input_path, "r");
        if (!in) {
            sched_error_set(err, "cannot open input file '%s'", opt->input_path);
            return SCHED_E_IO;
        }
        st = read_workload_csv(in, w, err);
        fclose(in);
    } else {
        /* Prompts are part of the legacy text output; keep machine formats clean. */
        FILE *prompts = opt->format == FORMAT_TEXT ? stdout : NULL;
        st = read_workload_interactive(stdin, prompts, w, err);
    }
    return st == SCHED_OK ? workload_validate(w, err) : st;
}

static SchedStatus write_file(const char *path, const ReportItem *items, size_t count, bool summary,
                              bool full, SchedError *err) {
    FILE *f = fopen(path, "w");
    if (!f) {
        sched_error_set(err, "cannot open '%s' for writing", path);
        return SCHED_E_IO;
    }
    if (summary) {
        write_summary_csv(f, items, count);
    } else {
        write_csv_header(f, full);
        for (size_t i = 0; i < count; i++) write_csv_rows(f, items[i].sim, full);
    }
    bool failed = ferror(f) != 0;
    if (fclose(f) != 0 || failed) {
        sched_error_set(err, "failed while writing '%s'", path);
        return SCHED_E_IO;
    }
    return SCHED_OK;
}

static SchedStatus write_stdout(const Options *opt, const ReportRun *run, const ReportItem *items,
                                size_t count, bool wrote_csv_file) {
    switch (opt->format) {
    case FORMAT_TEXT: {
        const TextOptions text = {
            .gantt = opt->gantt, .per_tick = opt->per_tick, .full = opt->full_metrics};
        if (!opt->input_path) fputs("\n", stdout); /* separates results from the prompts */
        for (size_t i = 0; i < count; i++) write_text_result(stdout, &items[i], &text);
        write_text_run(stdout, run);
        if (wrote_csv_file) printf("CSV written: %s\n", opt->csv_path);
        break;
    }
    case FORMAT_CSV:
        write_csv_header(stdout, opt->full_metrics);
        for (size_t i = 0; i < count; i++) write_csv_rows(stdout, items[i].sim, opt->full_metrics);
        break;
    case FORMAT_JSON:
        write_json(stdout, run, items, count);
        break;
    case FORMAT_NONE:
        break;
    }
    return fflush(stdout) == 0 && !ferror(stdout) ? SCHED_OK : SCHED_E_IO;
}

static double mean_turnaround(const SimResult *r) { return metrics_averages(r).turnaround; }

/* Runs the same policy with the oracle to measure what EWMA prediction cost. */
static SchedStatus regret(const Workload *w, const SimResult *r, double *out, SchedError *err) {
    SimOptions oracle = r->options;
    oracle.predict = PREDICT_ORACLE;
    SimResult base;
    SchedStatus st = sim_run(w, r->policy, &r->params, &oracle, &base, err);
    if (st != SCHED_OK) return st;
    double b = mean_turnaround(&base);
    *out = 100.0 * (mean_turnaround(r) - b) / b;
    sim_result_free(&base);
    return SCHED_OK;
}

static SchedStatus run(const Options *opt, SchedError *err) {
    Workload w;
    workload_init(&w);
    SimResult results[SCHED_MAX_POLICIES];
    ReportItem items[SCHED_MAX_POLICIES];
    ReportRun info = {.workload = &w};
    size_t count = 0;
    SchedStatus st = load_workload(opt, &w, err);

    for (size_t i = 0; st == SCHED_OK && i < policy_count(); i++) {
        if (!opt->selected[i]) continue;
        struct timespec t0, t1;
        timespec_get(&t0, TIME_UTC);
        st = sim_run(&w, policy_at(i), &opt->params, &opt->sim, &results[count], err);
        timespec_get(&t1, TIME_UTC);
        if (st == SCHED_OK && opt->timing) {
            double secs = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;
            fprintf(stderr, "timing %s %.6f\n", policy_at(i)->name, secs);
        }
        if (st == SCHED_OK) count++;
    }
    for (size_t i = 0; i < count; i++) items[i] = (ReportItem){.sim = &results[i]};

    if (st == SCHED_OK && opt->gap) {
        const Policy *opt_np = policy_find("opt-np");
        SimResult best;
        st = sim_run(&w, opt_np, &opt->params, &opt->sim, &best, err);
        if (st == SCHED_OK) {
            info.has_optimum = true;
            info.optimum_mean_turnaround = mean_turnaround(&best);
            sim_result_free(&best);
            for (size_t i = 0; i < count; i++) {
                items[i].has_gap = true;
                items[i].gap_pct = 100.0 *
                                   (mean_turnaround(&results[i]) - info.optimum_mean_turnaround) /
                                   info.optimum_mean_turnaround;
            }
        }
    }
    if (st == SCHED_OK && opt->sim.predict == PREDICT_EWMA) {
        info.has_prediction = true;
        info.prediction = metrics_prediction_error(&w, opt->sim.alpha, opt->sim.tau0);
        for (size_t i = 0; st == SCHED_OK && i < count; i++) {
            if (!results[i].policy->uses_estimates) continue;
            items[i].has_regret = true;
            st = regret(&w, &results[i], &items[i].regret_pct, err);
        }
    }
    for (size_t i = 0; st == SCHED_OK && i < count; i++) {
        st = metrics_summary(&results[i], &items[i].summary);
        if (st != SCHED_OK) sched_error_set(err, "out of memory while computing metrics");
    }

    bool csv_file = opt->csv_mode == CSV_FILE_EXPLICIT ||
                    (opt->csv_mode == CSV_FILE_DEFAULT && opt->format == FORMAT_TEXT);
    if (st == SCHED_OK && csv_file) {
        st = write_file(opt->csv_path, items, count, false, opt->full_metrics, err);
    }
    if (st == SCHED_OK && opt->summary_csv_path) {
        st = write_file(opt->summary_csv_path, items, count, true, false, err);
    }
    if (st == SCHED_OK) {
        st = write_stdout(opt, &info, items, count, csv_file);
        if (st != SCHED_OK) sched_error_set(err, "failed to write to standard output");
    }

    for (size_t i = 0; i < count; i++) sim_result_free(&results[i]);
    workload_free(&w);
    return st;
}

int main(int argc, char **argv) {
    const char *prog = argc > 0 ? argv[0] : "sched";
    Options opt;
    SchedError err = {{0}};
    SchedStatus st = parse_options(argc, argv, &opt, &err);
    if (st != SCHED_OK) {
        fprintf(stderr, "%s: error: %s\n", prog, err.msg);
        return (int)st;
    }
    if (opt.help) {
        print_usage(stdout, prog);
        return 0;
    }
    st = run(&opt, &err);
    if (st != SCHED_OK) {
        fflush(stdout);
        fprintf(stderr, "%s: error: %s\n", prog, err.msg);
    }
    return (int)st;
}
