#include "sched/metrics.h"

ProcMetrics metrics_for(const SimResult *r, size_t proc) {
    const Process *p = &r->workload->items[proc];
    const ProcOutcome *o = &r->outcomes[proc];
    ProcMetrics m = {
        .pid = p->pid,
        .arrival = p->arrival,
        .burst = p->burst,
        .start = o->start,
        .completion = o->completion,
        .response = o->start - p->arrival,
        .turnaround = o->completion - p->arrival,
    };
    m.waiting = m.turnaround - p->burst;
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
