#include "sched/metrics.h"

#include <stdlib.h>

ProcMetrics metrics_for(const SimResult *r, size_t proc) {
    const Process *p = &r->workload->items[proc];
    const ProcOutcome *o = &r->outcomes[proc];
    ProcMetrics m = {
        .pid = p->pid,
        .arrival = p->arrival,
        .burst = p->burst,
        .io = p->io,
        .start = o->start,
        .completion = o->completion,
        .response = o->start - p->arrival,
        .turnaround = o->completion - p->arrival,
    };
    m.waiting = m.turnaround - p->burst - p->io;
    m.slowdown = (double)m.turnaround / (double)p->burst;
    return m;
}

/* Accumulates in double, one process at a time in input order, exactly as
 * the original implementation did, so printed averages are unchanged. */
AvgMetrics metrics_averages(const SimResult *r) {
    double sr = 0, sw = 0, st = 0;
    const size_t n = r->workload->len;
    for (size_t i = 0; i < n; i++) {
        ProcMetrics m = metrics_for(r, i);
        sr += (double)m.response;
        sw += (double)m.waiting;
        st += (double)m.turnaround;
    }
    return (AvgMetrics){
        .response = sr / (double)n, .waiting = sw / (double)n, .turnaround = st / (double)n};
}

int64_t metrics_makespan(const SimResult *r) {
    int64_t makespan = 0;
    for (size_t i = 0; i < r->workload->len; i++) {
        if (r->outcomes[i].completion > makespan) makespan = r->outcomes[i].completion;
    }
    return makespan;
}

static int cmp_i64(const void *a, const void *b) {
    int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;
    return (x > y) - (x < y);
}

/* Nearest rank: the smallest value with at least pct% of values <= it. */
static int64_t percentile(const int64_t *sorted, size_t n, unsigned pct) {
    size_t rank = (pct * n + 99) / 100; /* ceil(pct * n / 100), 1-based */
    return sorted[rank ? rank - 1 : 0];
}

static Distribution distribution(int64_t *values, size_t n) {
    double sum = 0;
    for (size_t i = 0; i < n; i++) sum += (double)values[i];
    qsort(values, n, sizeof *values, cmp_i64);
    double median =
        n % 2 ? (double)values[n / 2] : ((double)values[n / 2 - 1] + (double)values[n / 2]) / 2.0;
    return (Distribution){.mean = sum / (double)n,
                          .median = median,
                          .p95 = percentile(values, n, 95),
                          .p99 = percentile(values, n, 99),
                          .max = values[n - 1]};
}

SchedStatus metrics_summary(const SimResult *r, Summary *out) {
    const size_t n = r->workload->len;
    int64_t *tat = malloc(n * sizeof *tat), *wait = malloc(n * sizeof *wait),
            *resp = malloc(n * sizeof *resp);
    if (!tat || !wait || !resp) {
        free(tat);
        free(wait);
        free(resp);
        return SCHED_E_NOMEM;
    }
    double sd_sum = 0, sd_sq = 0, sd_max = 0;
    int64_t cpu = 0;
    for (size_t i = 0; i < n; i++) {
        ProcMetrics m = metrics_for(r, i);
        tat[i] = m.turnaround;
        wait[i] = m.waiting;
        resp[i] = m.response;
        sd_sum += m.slowdown;
        sd_sq += m.slowdown * m.slowdown;
        if (m.slowdown > sd_max) sd_max = m.slowdown;
        cpu += m.burst;
    }
    *out = (Summary){0};
    out->turnaround = distribution(tat, n);
    out->waiting = distribution(wait, n);
    out->response = distribution(resp, n);
    out->slowdown_mean = sd_sum / (double)n;
    out->slowdown_max = sd_max;
    out->jain_slowdown = sd_sum * sd_sum / ((double)n * sd_sq);
    out->span = metrics_makespan(r) - r->first_arrival;
    double capacity = (double)r->cores * (double)out->span;
    out->throughput = (double)n / (double)out->span;
    out->utilization = (double)cpu / capacity;
    out->switches = r->switches;
    out->cs_time = r->cs_time;
    out->cs_fraction = (double)r->cs_time / capacity;
    free(tat);
    free(wait);
    free(resp);
    return SCHED_OK;
}

PredictionError metrics_prediction_error(const Workload *w, double alpha, double tau0) {
    PredictionError e = {0};
    double abs_sum = 0, pct_sum = 0;
    for (size_t i = 0; i < w->len; i++) {
        const int64_t *ph = workload_phases(w, i);
        double tau = tau0;
        for (size_t k = 0; k < w->items[i].phase_count; k += 2) {
            double actual = (double)ph[k];
            double err = tau > actual ? tau - actual : actual - tau;
            abs_sum += err;
            pct_sum += err / actual;
            e.bursts++;
            tau = alpha * actual + (1.0 - alpha) * tau;
        }
    }
    e.mae = abs_sum / (double)e.bursts;
    e.mape = 100.0 * pct_sum / (double)e.bursts;
    return e;
}
