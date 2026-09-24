/* Workload input parsers and result writers (text, CSV, JSON). */
#ifndef SCHED_IO_H
#define SCHED_IO_H

#include <stdio.h>

#include "sched/engine.h"
#include "sched/metrics.h"

/* ---- input ---- */

/* Legacy interactive format: a process count followed by `pid arrival burst`
 * triples, all whitespace-separated. When `prompts` is non-NULL the original
 * prompts are written to it. Each process is range-checked as it is read;
 * whole-workload checks (duplicates, overflow) are left to workload_validate. */
SchedStatus read_workload_interactive(FILE *in, FILE *prompts, Workload *w, SchedError *err);

/* CSV with a header row naming the columns (any order, case-insensitive):
 *   pid, arrival             required
 *   burst | bursts           at least one; `bursts` is cpu:io:cpu:...:cpu and
 *                            wins when both are present and non-empty
 *   priority, tickets, nice  optional (defaults in core.h)
 * Blank lines and lines starting with '#' are ignored. Fields are plain
 * values (no quoting); an empty optional field takes its default. */
SchedStatus read_workload_csv(FILE *in, Workload *w, SchedError *err);

/* Parses a base-10 int64 occupying the whole string. */
bool parse_i64(const char *s, int64_t *out);

/* ---- output ---- */

/* Per-result data beyond the simulation itself. */
typedef struct {
    const SimResult *sim;
    Summary summary;
    bool has_gap;
    double gap_pct; /* mean turnaround vs opt-np, percent above optimum; negative = below */
    bool has_regret;
    double regret_pct; /* mean turnaround with EWMA vs oracle, percent */
} ReportItem;

/* Whole-run data shared by all results. */
typedef struct {
    const Workload *workload;
    bool has_prediction;
    PredictionError prediction;
    bool has_optimum;
    double optimum_mean_turnaround;
} ReportRun;

/* "FCFS", "RoundRobin(q=2)", ... as used in CSV rows and averages. */
void result_label(const SimResult *r, char *buf, size_t size);

typedef struct {
    bool gantt;
    bool per_tick;
    bool full; /* --metrics=full */
} TextOptions;

void write_text_result(FILE *out, const ReportItem *item, const TextOptions *opt);
void write_text_run(FILE *out, const ReportRun *run); /* run-level lines (prediction error) */

/* Per-process CSV; `full` adds IO and Slowdown columns. */
void write_csv_header(FILE *out, bool full);
void write_csv_rows(FILE *out, const SimResult *r, bool full);

/* One row per result with every aggregate metric. */
void write_summary_csv(FILE *out, const ReportItem *items, size_t count);

/* One JSON document covering all results; schema in docs/json-schema.md. */
void write_json(FILE *out, const ReportRun *run, const ReportItem *items, size_t count);

#endif
