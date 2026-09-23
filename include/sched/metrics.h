/* Per-process and aggregate scheduling metrics derived from a SimResult. */
#ifndef SCHED_METRICS_H
#define SCHED_METRICS_H

#include "sched/engine.h"

typedef struct {
    int64_t pid;
    int64_t arrival;
    int64_t burst;
    int64_t start;
    int64_t completion;
    int64_t response;   /* start - arrival */
    int64_t turnaround; /* completion - arrival */
    int64_t waiting;    /* turnaround - burst */
} ProcMetrics;

typedef struct {
    double response;
    double waiting;
    double turnaround;
} AvgMetrics;

ProcMetrics metrics_for(const SimResult *r, size_t proc);
AvgMetrics metrics_averages(const SimResult *r);
int64_t metrics_makespan(const SimResult *r); /* latest completion time */

#endif
