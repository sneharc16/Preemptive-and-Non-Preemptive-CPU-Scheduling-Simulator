/* Event-driven single-CPU simulation engine. */
#ifndef SCHED_ENGINE_H
#define SCHED_ENGINE_H

#include "sched/core.h"
#include "sched/policy.h"

#define SCHED_IDLE SIZE_MAX /* Segment.proc value for an idle CPU */

/* Half-open interval [start, end) during which `proc` ran (or the CPU idled). */
typedef struct {
    int64_t start;
    int64_t end;
    size_t proc; /* index into the workload, or SCHED_IDLE */
} Segment;

/* Ordered, contiguous, coalesced (no two neighbours share proc) segments that
 * cover [first arrival, makespan]. */
typedef struct {
    Segment *items;
    size_t len;
    size_t cap;
} Timeline;

typedef struct {
    int64_t start;      /* first time on the CPU */
    int64_t completion; /* time the last tick finished */
} ProcOutcome;

typedef struct {
    const Policy *policy;
    const Workload *workload; /* borrowed; must outlive the result */
    ProcOutcome *outcomes;    /* indexed like workload->items */
    Timeline timeline;
} SimResult;

/* Resource limits for one simulation. The timeline is stored in full, so a
 * workload that would need an absurd number of segments (e.g. RR with q=2 on
 * bursts of 2e9 ticks) fails with a clear error instead of exhausting memory. */
typedef struct {
    size_t max_segments;
} SimLimits;

#define SCHED_DEFAULT_MAX_SEGMENTS ((size_t)1 << 24) /* 16,777,216 segments (~400 MB) */

/* Runs `policy` over `w` with the default limits. The workload is validated
 * first (see workload_validate). On success the caller owns *out and must
 * release it with sim_result_free; on failure *out is left empty. */
SchedStatus sim_run(const Workload *w, const Policy *policy, const PolicyParams *params,
                    SimResult *out, SchedError *err);

/* As sim_run, with explicit limits. */
SchedStatus sim_run_limited(const Workload *w, const Policy *policy, const PolicyParams *params,
                            const SimLimits *limits, SimResult *out, SchedError *err);

void sim_result_free(SimResult *r);

#endif
