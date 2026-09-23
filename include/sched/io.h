/* Workload input parsers and result writers (text, CSV, JSON). */
#ifndef SCHED_IO_H
#define SCHED_IO_H

#include <stdio.h>

#include "sched/engine.h"

/* ---- input ---- */

/* Legacy interactive format: a process count followed by `pid arrival burst`
 * triples, all whitespace-separated. When `prompts` is non-NULL the original
 * prompts are written to it. Each process is range-checked as it is read;
 * whole-workload checks (duplicates, overflow) are left to workload_validate. */
SchedStatus read_workload_interactive(FILE *in, FILE *prompts, Workload *w, SchedError *err);

/* CSV with a header row naming the columns (any order): pid, arrival, burst.
 * Blank lines and lines starting with '#' are ignored. Fields are plain
 * integers (no quoting). */
SchedStatus read_workload_csv(FILE *in, Workload *w, SchedError *err);

/* Parses a base-10 int64 occupying the whole string. */
bool parse_i64(const char *s, int64_t *out);

/* ---- output ---- */

/* "FCFS", "RoundRobin(q=2)", ... as used in CSV rows and averages. */
void result_label(const SimResult *r, const PolicyParams *params, char *buf, size_t size);

typedef struct {
    bool gantt;
    bool per_tick;
} TextOptions;

void write_text_result(FILE *out, const SimResult *r, const PolicyParams *params,
                       const TextOptions *opt);

void write_csv_header(FILE *out);
void write_csv_rows(FILE *out, const SimResult *r, const PolicyParams *params);

/* One JSON document covering all results; schema in docs/json-schema.md. */
void write_json(FILE *out, const Workload *w, const SimResult *results, size_t count,
                const PolicyParams *params);

#endif
