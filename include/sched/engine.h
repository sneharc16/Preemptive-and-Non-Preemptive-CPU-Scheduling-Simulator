/* Event-driven simulation engine: one or more CPUs, a single global ready
 * set owned by the policy, optional context-switch cost, I/O phases and
 * burst prediction. */
#ifndef SCHED_ENGINE_H
#define SCHED_ENGINE_H

#include "sched/core.h"
#include "sched/policy.h"

#define SCHED_NONE SIZE_MAX /* "no process" (idle segment, free core) */
#define SCHED_IDLE SCHED_NONE

typedef enum {
    SEG_RUN,  /* proc executed */
    SEG_IDLE, /* core idle; proc == SCHED_IDLE */
    SEG_CS    /* context switch into proc */
} SegmentKind;

/* Half-open interval [start, end) on one core. */
typedef struct {
    int64_t start;
    int64_t end;
    size_t proc; /* index into the workload, or SCHED_IDLE */
    SegmentKind kind;
} Segment;

/* Ordered, contiguous segments covering [first arrival, makespan]; adjacent
 * segments never share both kind and proc (they are merged). */
typedef struct {
    Segment *items;
    size_t len;
    size_t cap;
} Timeline;

typedef enum { PREDICT_ORACLE, PREDICT_EWMA } PredictMode;

typedef struct {
    size_t cores;        /* >= 1 */
    int64_t cs_cost;     /* ticks charged when a core switches to a different process */
    PredictMode predict; /* what uses_estimates policies see */
    double alpha;        /* EWMA weight of the newest burst, 0..1 */
    double tau0;         /* EWMA initial prediction, > 0 */
    size_t max_segments; /* cap on the total number of Gantt segments */
} SimOptions;

#define SCHED_DEFAULT_MAX_SEGMENTS ((size_t)1 << 24) /* 16,777,216 segments */
#define SCHED_MAX_CORES 1024

/* 1 core, no switch cost, oracle, alpha 0.5, tau0 5, default segment cap. */
void sim_options_default(SimOptions *o);

typedef struct {
    int64_t start;      /* first tick on a CPU (after any switch-in) */
    int64_t completion; /* end of the last CPU tick */
} ProcOutcome;

typedef struct {
    const Policy *policy;
    const Workload *workload; /* borrowed; must outlive the result */
    PolicyParams params;
    SimOptions options;
    ProcOutcome *outcomes; /* indexed like workload->items */
    Timeline *timelines;   /* one per core */
    size_t cores;
    int64_t first_arrival;
    int64_t switches; /* dispatches onto a core whose previous process was different */
    int64_t cs_time;  /* ticks spent switching, summed over cores */
} SimResult;

/* Runs `policy` over `w`. The workload is validated first (see
 * workload_validate). On success the caller owns *out and must release it
 * with sim_result_free; on failure *out is left empty. */
SchedStatus sim_run(const Workload *w, const Policy *policy, const PolicyParams *params,
                    const SimOptions *options, SimResult *out, SchedError *err);

void sim_result_free(SimResult *r);

#endif
