#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "sched/io.h"

void result_label(const SimResult *r, char *buf, size_t size) {
    if (r->policy->uses_quantum) {
        snprintf(buf, size, "%s(q=%" PRId64 ")", r->policy->label, r->params.quantum);
    } else {
        snprintf(buf, size, "%s", r->policy->label);
    }
}

static int64_t pid_of(const SimResult *r, size_t proc) { return r->workload->items[proc].pid; }

/* ===================== text ===================== */

/* With default options the text format reproduces the original program's
 * output byte for byte. */

static void core_suffix(char *buf, size_t size, const SimResult *r, size_t core) {
    if (r->cores > 1) {
        snprintf(buf, size, " (core %zu)", core);
    } else {
        buf[0] = '\0';
    }
}

static void write_gantt(FILE *out, const SimResult *r, const char *label) {
    for (size_t c = 0; c < r->cores; c++) {
        const Timeline *t = &r->timelines[c];
        char suffix[32];
        core_suffix(suffix, sizeof suffix, r, c);
        fprintf(out, "Gantt — %s%s:\n", label, suffix);
        for (size_t i = 0; i < t->len; i++) {
            const Segment *s = &t->items[i];
            fprintf(out, "[%-3" PRId64 ",%-3" PRId64 ") ", s->start, s->end);
            switch (s->kind) {
            case SEG_IDLE:
                fputs("IDLE  ", out);
                break;
            case SEG_CS:
                fputs("CS    ", out);
                break;
            case SEG_RUN:
                fprintf(out, "P%-4" PRId64, pid_of(r, s->proc));
                break;
            }
            if (i + 1 < t->len) fputs("| ", out);
        }
        fputs("\n\n", out);
    }
}

static void write_per_tick(FILE *out, const SimResult *r, const char *label) {
    for (size_t c = 0; c < r->cores; c++) {
        const Timeline *t = &r->timelines[c];
        char suffix[32];
        core_suffix(suffix, sizeof suffix, r, c);
        fprintf(out, "Per-tick timeline — %s%s:\n", label, suffix);
        for (size_t i = 0; i < t->len; i++) {
            const Segment *s = &t->items[i];
            for (int64_t tick = s->start; tick < s->end; tick++) {
                switch (s->kind) {
                case SEG_IDLE:
                    fprintf(out, "t=%" PRId64 ": IDLE\n", tick);
                    break;
                case SEG_CS:
                    fprintf(out, "t=%" PRId64 ": CS -> P%" PRId64 "\n", tick, pid_of(r, s->proc));
                    break;
                case SEG_RUN:
                    fprintf(out, "t=%" PRId64 ": P%" PRId64 "\n", tick, pid_of(r, s->proc));
                    break;
                }
            }
        }
        fputs("\n", out);
    }
}

static void write_distribution(FILE *out, const char *name, const Distribution *d) {
    fprintf(out,
            "  %-12smean %.2f  median %.2f  p95 %" PRId64 "  p99 %" PRId64 "  max %" PRId64 "\n",
            name, d->mean, d->median, d->p95, d->p99, d->max);
}

static void write_full_metrics(FILE *out, const ReportItem *item, const char *label) {
    const Summary *s = &item->summary;
    fprintf(out, "%s Metrics:\n", label);
    write_distribution(out, "Turnaround:", &s->turnaround);
    write_distribution(out, "Waiting:", &s->waiting);
    write_distribution(out, "Response:", &s->response);
    fprintf(out, "  Slowdown:   mean %.2f  max %.2f  Jain fairness %.4f\n", s->slowdown_mean,
            s->slowdown_max, s->jain_slowdown);
    fprintf(out, "  Throughput: %.4f processes/tick  CPU utilization: %.2f%%\n", s->throughput,
            100.0 * s->utilization);
    fprintf(out, "  Context switches: %" PRId64 " (%" PRId64 " ticks, %.2f%% of CPU time)\n\n",
            s->switches, s->cs_time, 100.0 * s->cs_fraction);
}

void write_text_result(FILE *out, const ReportItem *item, const TextOptions *opt) {
    const SimResult *r = item->sim;
    char label[64];
    result_label(r, label, sizeof label);
    if (r->policy->uses_quantum) {
        fprintf(out, "%s (q=%" PRId64 ") =>\n", r->policy->heading, r->params.quantum);
    } else {
        fprintf(out, "%s =>\n", r->policy->heading);
    }
    if (opt->gantt) write_gantt(out, r, label);
    if (opt->per_tick) write_per_tick(out, r, label);
    AvgMetrics avg = metrics_averages(r);
    fprintf(out, "%s Averages:\n  Response:  %.2f\n  Waiting :  %.2f\n  Turnaround:%.2f\n\n", label,
            avg.response, avg.waiting, avg.turnaround);
    if (opt->full) write_full_metrics(out, item, label);
    if (item->has_gap) {
        if (item->gap_pct < 0) {
            fprintf(out,
                    "%s optimality gap: %.2f%% below the opt-np mean turnaround "
                    "(preemption can beat the non-preemptive optimum)\n\n",
                    label, -item->gap_pct);
        } else {
            fprintf(out, "%s optimality gap: %.2f%% above the opt-np mean turnaround\n\n", label,
                    item->gap_pct);
        }
    }
    if (item->has_regret) {
        fprintf(out, "%s prediction regret: %.2f%% mean turnaround vs oracle\n\n", label,
                item->regret_pct);
    }
}

void write_text_run(FILE *out, const ReportRun *run) {
    if (run->has_prediction) {
        fprintf(out,
                "EWMA burst prediction: MAE %.4f ticks, MAPE %.2f%% over %" PRId64
                " CPU bursts\n\n",
                run->prediction.mae, run->prediction.mape, run->prediction.bursts);
    }
    if (run->has_optimum) {
        fprintf(out, "opt-np optimum mean turnaround: %.2f\n\n", run->optimum_mean_turnaround);
    }
}

/* ===================== CSV ===================== */

void write_csv_header(FILE *out, bool full) {
    fputs("Algorithm,PID,Arrival,Burst,Start,Completion,Response,Waiting,Turnaround", out);
    fputs(full ? ",IO,Slowdown\n" : "\n", out);
}

void write_csv_rows(FILE *out, const SimResult *r, bool full) {
    char label[64];
    result_label(r, label, sizeof label);
    for (size_t i = 0; i < r->workload->len; i++) {
        ProcMetrics m = metrics_for(r, i);
        fprintf(out,
                "%s,%" PRId64 ",%" PRId64 ",%" PRId64 ",%" PRId64 ",%" PRId64 ",%" PRId64
                ",%" PRId64 ",%" PRId64,
                label, m.pid, m.arrival, m.burst, m.start, m.completion, m.response, m.waiting,
                m.turnaround);
        if (full) fprintf(out, ",%" PRId64 ",%.6f", m.io, m.slowdown);
        fputs("\n", out);
    }
}

static void csv_distribution(FILE *out, const Distribution *d) {
    fprintf(out, ",%.6f,%.6f,%" PRId64 ",%" PRId64 ",%" PRId64, d->mean, d->median, d->p95, d->p99,
            d->max);
}

void write_summary_csv(FILE *out, const ReportItem *items, size_t count) {
    fputs("Algorithm", out);
    const char *dists[] = {"Turnaround", "Waiting", "Response"};
    for (size_t k = 0; k < 3; k++) {
        fprintf(out, ",%sMean,%sMedian,%sP95,%sP99,%sMax", dists[k], dists[k], dists[k], dists[k],
                dists[k]);
    }
    fputs(",SlowdownMean,SlowdownMax,JainSlowdown,Throughput,Utilization,ContextSwitches,"
          "SwitchTime,SwitchFraction,GapPct,RegretPct\n",
          out);
    for (size_t i = 0; i < count; i++) {
        const Summary *s = &items[i].summary;
        char label[64];
        result_label(items[i].sim, label, sizeof label);
        fputs(label, out);
        csv_distribution(out, &s->turnaround);
        csv_distribution(out, &s->waiting);
        csv_distribution(out, &s->response);
        fprintf(out, ",%.6f,%.6f,%.6f,%.6f,%.6f,%" PRId64 ",%" PRId64 ",%.6f", s->slowdown_mean,
                s->slowdown_max, s->jain_slowdown, s->throughput, s->utilization, s->switches,
                s->cs_time, s->cs_fraction);
        if (items[i].has_gap) {
            fprintf(out, ",%.6f", items[i].gap_pct);
        } else {
            fputs(",", out);
        }
        if (items[i].has_regret) {
            fprintf(out, ",%.6f\n", items[i].regret_pct);
        } else {
            fputs(",\n", out);
        }
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

static void json_key_double(FILE *out, const char *key, double v, const char *after) {
    fprintf(out, "\"%s\": ", key);
    json_double(out, v);
    fputs(after, out);
}

static void json_distribution(FILE *out, const char *key, const Distribution *d) {
    fprintf(out, "        \"%s\": {", key);
    json_key_double(out, "mean", d->mean, ", ");
    json_key_double(out, "median", d->median, ", ");
    fprintf(out, "\"p95\": %" PRId64 ", \"p99\": %" PRId64 ", \"max\": %" PRId64 "},\n", d->p95,
            d->p99, d->max);
}

static const char *kind_name(SegmentKind k) {
    switch (k) {
    case SEG_IDLE:
        return "idle";
    case SEG_CS:
        return "cs";
    case SEG_RUN:
        break;
    }
    return "run";
}

static void json_result(FILE *out, const ReportItem *item) {
    const SimResult *r = item->sim;
    const Summary *s = &item->summary;
    char label[64];
    result_label(r, label, sizeof label);
    /* Policy names and labels are fixed ASCII identifiers: no escaping needed. */
    fprintf(out, "    {\n      \"algorithm\": \"%s\",\n      \"label\": \"%s\",\n", r->policy->name,
            label);
    fprintf(out, "      \"preemptive\": %s,\n", r->policy->preemptive ? "true" : "false");
    if (r->policy->uses_quantum) {
        fprintf(out, "      \"quantum\": %" PRId64 ",\n", r->params.quantum);
    } else {
        fputs("      \"quantum\": null,\n", out);
    }
    fputs("      \"processes\": [\n", out);
    for (size_t i = 0; i < r->workload->len; i++) {
        ProcMetrics m = metrics_for(r, i);
        fprintf(out,
                "        {\"pid\": %" PRId64 ", \"arrival\": %" PRId64 ", \"burst\": %" PRId64
                ", \"io\": %" PRId64 ", \"start\": %" PRId64 ", \"completion\": %" PRId64
                ", \"response\": %" PRId64 ", \"waiting\": %" PRId64 ", \"turnaround\": %" PRId64
                ", ",
                m.pid, m.arrival, m.burst, m.io, m.start, m.completion, m.response, m.waiting,
                m.turnaround);
        json_key_double(out, "slowdown", m.slowdown, i + 1 < r->workload->len ? "},\n" : "}\n");
    }
    AvgMetrics avg = metrics_averages(r);
    fputs("      ],\n      \"averages\": {", out);
    json_key_double(out, "response", avg.response, ", ");
    json_key_double(out, "waiting", avg.waiting, ", ");
    json_key_double(out, "turnaround", avg.turnaround, "},\n");
    fputs("      \"summary\": {\n", out);
    json_distribution(out, "turnaround", &s->turnaround);
    json_distribution(out, "waiting", &s->waiting);
    json_distribution(out, "response", &s->response);
    fputs("        ", out);
    json_key_double(out, "slowdown_mean", s->slowdown_mean, ", ");
    json_key_double(out, "slowdown_max", s->slowdown_max, ", ");
    json_key_double(out, "jain_fairness_slowdown", s->jain_slowdown, ",\n        ");
    fprintf(out, "\"span\": %" PRId64 ", ", s->span);
    json_key_double(out, "throughput", s->throughput, ", ");
    json_key_double(out, "cpu_utilization", s->utilization, ",\n        ");
    fprintf(out, "\"context_switches\": %" PRId64 ", \"switch_time\": %" PRId64 ", ", s->switches,
            s->cs_time);
    json_key_double(out, "switch_fraction", s->cs_fraction, "\n      },\n");
    if (item->has_gap) {
        fputs("      ", out);
        json_key_double(out, "optimality_gap_pct", item->gap_pct, ",\n");
    }
    if (item->has_regret) {
        fputs("      ", out);
        json_key_double(out, "prediction_regret_pct", item->regret_pct, ",\n");
    }
    fprintf(out, "      \"makespan\": %" PRId64 ",\n      \"cores\": %zu,\n      \"gantt\": [\n",
            metrics_makespan(r), r->cores);
    for (size_t c = 0; c < r->cores; c++) {
        const Timeline *t = &r->timelines[c];
        for (size_t i = 0; i < t->len; i++) {
            const Segment *seg = &t->items[i];
            fprintf(out,
                    "        {\"start\": %" PRId64 ", \"end\": %" PRId64 ", \"pid\": ", seg->start,
                    seg->end);
            if (seg->kind == SEG_IDLE) {
                fputs("null", out);
            } else {
                fprintf(out, "%" PRId64, pid_of(r, seg->proc));
            }
            bool last = c + 1 == r->cores && i + 1 == t->len;
            fprintf(out, ", \"type\": \"%s\", \"core\": %zu}%s\n", kind_name(seg->kind), c,
                    last ? "" : ",");
        }
    }
    fputs("      ]\n    }", out);
}

static void json_workload(FILE *out, const Workload *w) {
    fputs("  \"workload\": [\n", out);
    for (size_t i = 0; i < w->len; i++) {
        const Process *p = &w->items[i];
        fprintf(out,
                "    {\"pid\": %" PRId64 ", \"arrival\": %" PRId64 ", \"burst\": %" PRId64
                ", \"priority\": %" PRId64 ", \"tickets\": %" PRId64 ", \"nice\": %" PRId64
                ", \"bursts\": [",
                p->pid, p->arrival, p->burst, p->priority, p->tickets, p->nice);
        const int64_t *ph = workload_phases(w, i);
        for (size_t k = 0; k < p->phase_count; k++) {
            fprintf(out, "%s%" PRId64, k ? ", " : "", ph[k]);
        }
        fprintf(out, "]}%s\n", i + 1 < w->len ? "," : "");
    }
    fputs("  ],\n", out);
}

void write_json(FILE *out, const ReportRun *run, const ReportItem *items, size_t count) {
    fputs("{\n  \"schema_version\": 1,\n", out);
    json_workload(out, run->workload);
    if (count > 0) {
        const SimOptions *o = &items[0].sim->options;
        fprintf(out,
                "  \"options\": {\"cores\": %zu, \"cs_cost\": %" PRId64 ", \"predict\": ", o->cores,
                o->cs_cost);
        if (o->predict == PREDICT_EWMA) {
            fputs("\"ewma\", ", out);
            json_key_double(out, "alpha", o->alpha, ", ");
            json_key_double(out, "tau0", o->tau0, "},\n");
        } else {
            fputs("\"oracle\"},\n", out);
        }
    }
    if (run->has_prediction) {
        fprintf(out, "  \"prediction\": {\"bursts\": %" PRId64 ", ", run->prediction.bursts);
        json_key_double(out, "mae", run->prediction.mae, ", ");
        json_key_double(out, "mape_pct", run->prediction.mape, "},\n");
    }
    if (run->has_optimum) {
        fputs("  ", out);
        json_key_double(out, "optimum_mean_turnaround", run->optimum_mean_turnaround, ",\n");
    }
    fputs("  \"results\": [\n", out);
    for (size_t i = 0; i < count; i++) {
        json_result(out, &items[i]);
        fputs(i + 1 < count ? ",\n" : "\n", out);
    }
    fputs("  ]\n}\n", out);
}
