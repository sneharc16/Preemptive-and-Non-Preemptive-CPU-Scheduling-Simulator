#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "sched/io.h"
#include "sched/metrics.h"

void result_label(const SimResult *r, const PolicyParams *params, char *buf, size_t size) {
    if (r->policy->uses_quantum) {
        snprintf(buf, size, "%s(q=%" PRId64 ")", r->policy->label, params->quantum);
    } else {
        snprintf(buf, size, "%s", r->policy->label);
    }
}

static int64_t segment_pid(const SimResult *r, const Segment *s) {
    return r->workload->items[s->proc].pid;
}

/* ===================== text ===================== */

/* The text format reproduces the original program's output byte for byte. */

static void write_gantt(FILE *out, const SimResult *r, const char *label) {
    const Timeline *t = &r->timeline;
    fprintf(out, "Gantt — %s:\n", label);
    for (size_t i = 0; i < t->len; i++) {
        const Segment *s = &t->items[i];
        if (s->proc == SCHED_IDLE) {
            fprintf(out, "[%-3" PRId64 ",%-3" PRId64 ") IDLE  ", s->start, s->end);
        } else {
            fprintf(out, "[%-3" PRId64 ",%-3" PRId64 ") P%-4" PRId64, s->start, s->end,
                    segment_pid(r, s));
        }
        if (i + 1 < t->len) fputs("| ", out);
    }
    fputs("\n\n", out);
}

static void write_per_tick(FILE *out, const SimResult *r, const char *label) {
    const Timeline *t = &r->timeline;
    fprintf(out, "Per-tick timeline — %s:\n", label);
    for (size_t i = 0; i < t->len; i++) {
        const Segment *s = &t->items[i];
        for (int64_t tick = s->start; tick < s->end; tick++) {
            if (s->proc == SCHED_IDLE) {
                fprintf(out, "t=%" PRId64 ": IDLE\n", tick);
            } else {
                fprintf(out, "t=%" PRId64 ": P%" PRId64 "\n", tick, segment_pid(r, s));
            }
        }
    }
    fputs("\n", out);
}

void write_text_result(FILE *out, const SimResult *r, const PolicyParams *params,
                       const TextOptions *opt) {
    char label[64];
    result_label(r, params, label, sizeof label);
    if (r->policy->uses_quantum) {
        fprintf(out, "%s (q=%" PRId64 ") =>\n", r->policy->heading, params->quantum);
    } else {
        fprintf(out, "%s =>\n", r->policy->heading);
    }
    if (opt->gantt) write_gantt(out, r, label);
    if (opt->per_tick) write_per_tick(out, r, label);
    AvgMetrics avg = metrics_averages(r);
    fprintf(out, "%s Averages:\n  Response:  %.2f\n  Waiting :  %.2f\n  Turnaround:%.2f\n\n", label,
            avg.response, avg.waiting, avg.turnaround);
}

/* ===================== CSV ===================== */

void write_csv_header(FILE *out) {
    fputs("Algorithm,PID,Arrival,Burst,Start,Completion,Response,Waiting,Turnaround\n", out);
}

void write_csv_rows(FILE *out, const SimResult *r, const PolicyParams *params) {
    char label[64];
    result_label(r, params, label, sizeof label);
    for (size_t i = 0; i < r->workload->len; i++) {
        ProcMetrics m = metrics_for(r, i);
        fprintf(out,
                "%s,%" PRId64 ",%" PRId64 ",%" PRId64 ",%" PRId64 ",%" PRId64 ",%" PRId64
                ",%" PRId64 ",%" PRId64 "\n",
                label, m.pid, m.arrival, m.burst, m.start, m.completion, m.response, m.waiting,
                m.turnaround);
    }
}

/* ===================== JSON ===================== */

/* Shortest of %.15g / %.17g that round-trips exactly. */
static void json_double(FILE *out, double v) {
    char buf[40];
    snprintf(buf, sizeof buf, "%.15g", v);
    if (strtod(buf, NULL) != v) snprintf(buf, sizeof buf, "%.17g", v);
    fputs(buf, out);
}

static void json_result(FILE *out, const SimResult *r, const PolicyParams *params) {
    char label[64];
    result_label(r, params, label, sizeof label);
    /* Policy names and labels are fixed ASCII identifiers: no escaping needed. */
    fprintf(out, "    {\n      \"algorithm\": \"%s\",\n      \"label\": \"%s\",\n", r->policy->name,
            label);
    fprintf(out, "      \"preemptive\": %s,\n", r->policy->preemptive ? "true" : "false");
    if (r->policy->uses_quantum) {
        fprintf(out, "      \"quantum\": %" PRId64 ",\n", params->quantum);
    } else {
        fputs("      \"quantum\": null,\n", out);
    }
    fputs("      \"processes\": [\n", out);
    for (size_t i = 0; i < r->workload->len; i++) {
        ProcMetrics m = metrics_for(r, i);
        fprintf(out,
                "        {\"pid\": %" PRId64 ", \"arrival\": %" PRId64 ", \"burst\": %" PRId64
                ", \"start\": %" PRId64 ", \"completion\": %" PRId64 ", \"response\": %" PRId64
                ", \"waiting\": %" PRId64 ", \"turnaround\": %" PRId64 "}%s\n",
                m.pid, m.arrival, m.burst, m.start, m.completion, m.response, m.waiting,
                m.turnaround, i + 1 < r->workload->len ? "," : "");
    }
    AvgMetrics avg = metrics_averages(r);
    fputs("      ],\n      \"averages\": {\"response\": ", out);
    json_double(out, avg.response);
    fputs(", \"waiting\": ", out);
    json_double(out, avg.waiting);
    fputs(", \"turnaround\": ", out);
    json_double(out, avg.turnaround);
    fprintf(out, "},\n      \"makespan\": %" PRId64 ",\n      \"gantt\": [\n", metrics_makespan(r));
    const Timeline *t = &r->timeline;
    for (size_t i = 0; i < t->len; i++) {
        const Segment *s = &t->items[i];
        fprintf(out, "        {\"start\": %" PRId64 ", \"end\": %" PRId64 ", \"pid\": ", s->start,
                s->end);
        if (s->proc == SCHED_IDLE) {
            fputs("null", out);
        } else {
            fprintf(out, "%" PRId64, segment_pid(r, s));
        }
        fprintf(out, "}%s\n", i + 1 < t->len ? "," : "");
    }
    fputs("      ]\n    }", out);
}

void write_json(FILE *out, const Workload *w, const SimResult *results, size_t count,
                const PolicyParams *params) {
    fputs("{\n  \"schema_version\": 1,\n  \"workload\": [\n", out);
    for (size_t i = 0; i < w->len; i++) {
        const Process *p = &w->items[i];
        fprintf(out,
                "    {\"pid\": %" PRId64 ", \"arrival\": %" PRId64 ", \"burst\": %" PRId64 "}%s\n",
                p->pid, p->arrival, p->burst, i + 1 < w->len ? "," : "");
    }
    fputs("  ],\n  \"results\": [\n", out);
    for (size_t i = 0; i < count; i++) {
        json_result(out, &results[i], params);
        fputs(i + 1 < count ? ",\n" : "\n", out);
    }
    fputs("  ]\n}\n", out);
}
