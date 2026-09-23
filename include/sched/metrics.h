/* Per-process and aggregate scheduling metrics derived from a SimResult.
 *
 * Definitions (all times in ticks):
 *   response   = first time on a CPU - arrival
 *   turnaround = final completion - arrival
 *   waiting    = turnaround - total CPU - total I/O   (time ready but not
 *                running, including switch-in overhead)
 *   slowdown   = turnaround / total CPU
 * Percentiles use the nearest-rank method on the sorted per-process values;
 * the median is the mean of the two middle values when n is even.
 */
#ifndef SCHED_METRICS_H
#define SCHED_METRICS_H

#include "sched/engine.h"

typedef struct {
    int64_t pid;
    int64_t arrival;
    int64_t burst; /* total CPU */
    int64_t io;    /* total I/O */
    int64_t start;
    int64_t completion;
    int64_t response;
    int64_t turnaround;
    int64_t waiting;
    double slowdown;
} ProcMetrics;

typedef struct {
    double response;
    double waiting;
    double turnaround;
} AvgMetrics;

typedef struct {
    double mean;
    double median;
    int64_t p95;
    int64_t p99;
    int64_t max;
} Distribution;

typedef struct {
    Distribution turnaround;
    Distribution waiting;
    Distribution response;
    double slowdown_mean;
    double slowdown_max;
    double jain_slowdown; /* Jain's fairness index over per-process slowdown, in (0, 1] */
    int64_t span;         /* makespan - first arrival */
    double throughput;    /* processes completed per tick over the span */
    double utilization;   /* busy (running) core-ticks / (cores * span) */
    int64_t switches;     /* dispatches that changed a core's process */
    int64_t cs_time;      /* core-ticks spent switching */
    double cs_fraction;   /* cs_time / (cores * span) */
} Summary;

typedef struct {
    int64_t bursts; /* CPU bursts predicted */
    double mae;     /* mean |prediction - actual| */
    double mape;    /* mean |prediction - actual| / actual, in percent */
} PredictionError;

ProcMetrics metrics_for(const SimResult *r, size_t proc);
AvgMetrics metrics_averages(const SimResult *r);
int64_t metrics_makespan(const SimResult *r); /* latest completion time */

/* Returns SCHED_E_NOMEM if the scratch space for sorting cannot be allocated. */
SchedStatus metrics_summary(const SimResult *r, Summary *out);

/* EWMA prediction error over every CPU burst of the workload. Predictions
 * depend only on each process's own burst history, so this is independent of
 * the scheduling policy. */
PredictionError metrics_prediction_error(const Workload *w, double alpha, double tau0);

#endif
