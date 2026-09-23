/* sched: command-line front end for the scheduling simulator. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sched/engine.h"
#include "sched/io.h"
#include "sched/policy.h"

#define DEFAULT_CSV_PATH "schedule_metrics.csv"

typedef enum { FORMAT_TEXT, FORMAT_CSV, FORMAT_JSON } Format;
typedef enum { CSV_FILE_DEFAULT, CSV_FILE_EXPLICIT, CSV_FILE_OFF } CsvFileMode;

typedef struct {
    bool selected[SCHED_MAX_POLICIES];
    PolicyParams params;
    const char *input_path; /* NULL: interactive format on stdin */
    Format format;
    CsvFileMode csv_mode;
    const char *csv_path;
    bool gantt;
    bool per_tick;
    bool help;
} Options;

static void print_usage(FILE *out, const char *prog) {
    fprintf(out,
            "Usage: %s [options]\n"
            "Simulates CPU scheduling policies on a workload read from stdin (interactive\n"
            "format: N, then N lines of 'PID Arrival Burst') or from --input=FILE.\n"
            "\n"
            "Options:\n"
            "  --algo=LIST     comma-separated policies to run, or 'all' (default).\n"
            "                  Available:",
            prog);
    for (size_t i = 0; i < policy_count(); i++) fprintf(out, " %s", policy_at(i)->name);
    fputs("\n"
          "  --quantum=Q     time quantum for quantum-based policies, integer > 0 (default 2)\n"
          "  --input=FILE    read a CSV workload with header pid,arrival,burst\n"
          "  --format=FMT    stdout format: text (default), csv or json\n"
          "  --csv=FILE      also write per-process metrics as CSV to FILE; in text format\n"
          "                  " DEFAULT_CSV_PATH " is written by default\n"
          "  --no-csv        do not write a CSV file\n"
          "  --no-gantt      omit Gantt charts from text output\n"
          "  --per-tick      add a per-tick timeline to text output\n"
          "  -h, --help      show this help and exit\n"
          "\n"
          "Exit status: 0 success, 1 invalid input, 2 invalid usage, 3 I/O error,\n"
          "4 out of memory.\n",
          out);
}

static const char *flag_value(const char *arg, const char *flag) {
    size_t len = strlen(flag);
    return strncmp(arg, flag, len) == 0 ? arg + len : NULL;
}

static SchedStatus parse_algos(Options *opt, const char *list, SchedError *err) {
    memset(opt->selected, 0, sizeof opt->selected);
    if (strcmp(list, "all") == 0) {
        for (size_t i = 0; i < policy_count(); i++) opt->selected[i] = true;
        return SCHED_OK;
    }
    const char *p = list;
    for (;;) {
        size_t len = strcspn(p, ",");
        bool found = false;
        for (size_t i = 0; i < policy_count(); i++) {
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

static SchedStatus parse_options(int argc, char **argv, Options *opt, SchedError *err) {
    *opt = (Options){.params = {.quantum = 2},
                     .format = FORMAT_TEXT,
                     .csv_mode = CSV_FILE_DEFAULT,
                     .csv_path = DEFAULT_CSV_PATH,
                     .gantt = true};
    for (size_t i = 0; i < policy_count(); i++) opt->selected[i] = true;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i], *v;
        SchedStatus st;
        if ((v = flag_value(arg, "--algo="))) {
            if ((st = parse_algos(opt, v, err)) != SCHED_OK) return st;
        } else if ((v = flag_value(arg, "--quantum="))) {
            if (!parse_i64(v, &opt->params.quantum) || opt->params.quantum <= 0) {
                sched_error_set(err, "--quantum must be an integer > 0 (got '%s')", v);
                return SCHED_E_USAGE;
            }
        } else if ((v = flag_value(arg, "--input="))) {
            if (!*v) {
                sched_error_set(err, "--input needs a file name");
                return SCHED_E_USAGE;
            }
            opt->input_path = v;
        } else if ((v = flag_value(arg, "--format="))) {
            if (strcmp(v, "text") == 0) {
                opt->format = FORMAT_TEXT;
            } else if (strcmp(v, "csv") == 0) {
                opt->format = FORMAT_CSV;
            } else if (strcmp(v, "json") == 0) {
                opt->format = FORMAT_JSON;
            } else {
                sched_error_set(err, "--format must be text, csv or json (got '%s')", v);
                return SCHED_E_USAGE;
            }
        } else if ((v = flag_value(arg, "--csv="))) {
            if (!*v) {
                sched_error_set(err, "--csv needs a file name");
                return SCHED_E_USAGE;
            }
            opt->csv_mode = CSV_FILE_EXPLICIT;
            opt->csv_path = v;
        } else if (strcmp(arg, "--no-csv") == 0) {
            opt->csv_mode = CSV_FILE_OFF;
        } else if (strcmp(arg, "--no-gantt") == 0) {
            opt->gantt = false;
        } else if (strcmp(arg, "--per-tick") == 0) {
            opt->per_tick = true;
        } else if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            opt->help = true;
        } else {
            sched_error_set(err, "unknown option '%s' (see --help)", arg);
            return SCHED_E_USAGE;
        }
    }
    return SCHED_OK;
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

static SchedStatus write_csv_file(const char *path, const SimResult *results, size_t count,
                                  const PolicyParams *params, SchedError *err) {
    FILE *f = fopen(path, "w");
    if (!f) {
        sched_error_set(err, "cannot open '%s' for writing", path);
        return SCHED_E_IO;
    }
    write_csv_header(f);
    for (size_t i = 0; i < count; i++) write_csv_rows(f, &results[i], params);
    bool failed = ferror(f) != 0;
    if (fclose(f) != 0 || failed) {
        sched_error_set(err, "failed while writing '%s'", path);
        return SCHED_E_IO;
    }
    return SCHED_OK;
}

static SchedStatus write_stdout(const Options *opt, const Workload *w, const SimResult *results,
                                size_t count, bool wrote_csv_file) {
    switch (opt->format) {
    case FORMAT_TEXT: {
        const TextOptions text = {.gantt = opt->gantt, .per_tick = opt->per_tick};
        if (!opt->input_path) fputs("\n", stdout); /* separates results from the prompts */
        for (size_t i = 0; i < count; i++)
            write_text_result(stdout, &results[i], &opt->params, &text);
        if (wrote_csv_file) printf("CSV written: %s\n", opt->csv_path);
        break;
    }
    case FORMAT_CSV:
        write_csv_header(stdout);
        for (size_t i = 0; i < count; i++) write_csv_rows(stdout, &results[i], &opt->params);
        break;
    case FORMAT_JSON:
        write_json(stdout, w, results, count, &opt->params);
        break;
    }
    return fflush(stdout) == 0 && !ferror(stdout) ? SCHED_OK : SCHED_E_IO;
}

static SchedStatus run(const Options *opt, SchedError *err) {
    Workload w;
    workload_init(&w);
    SimResult results[SCHED_MAX_POLICIES];
    size_t count = 0;
    SchedStatus st = load_workload(opt, &w, err);

    for (size_t i = 0; st == SCHED_OK && i < policy_count(); i++) {
        if (!opt->selected[i]) continue;
        st = sim_run(&w, policy_at(i), &opt->params, &results[count], err);
        if (st == SCHED_OK) count++;
    }

    bool csv_file = opt->csv_mode == CSV_FILE_EXPLICIT ||
                    (opt->csv_mode == CSV_FILE_DEFAULT && opt->format == FORMAT_TEXT);
    if (st == SCHED_OK && csv_file) {
        st = write_csv_file(opt->csv_path, results, count, &opt->params, err);
    }
    if (st == SCHED_OK) {
        st = write_stdout(opt, &w, results, count, csv_file);
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
