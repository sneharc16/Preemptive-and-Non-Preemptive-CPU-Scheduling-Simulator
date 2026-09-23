/* Core data types shared by every module: processes, workloads, errors. */
#ifndef SCHED_CORE_H
#define SCHED_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__GNUC__) || defined(__clang__)
#define SCHED_PRINTF(fmt_idx, arg_idx) __attribute__((format(printf, fmt_idx, arg_idx)))
#else
#define SCHED_PRINTF(fmt_idx, arg_idx)
#endif

/* Defaults for the optional input columns. */
#define SCHED_DEFAULT_PRIORITY 0
#define SCHED_DEFAULT_TICKETS 100
#define SCHED_DEFAULT_NICE 0
#define SCHED_MAX_TICKETS ((int64_t)1 << 20)
#define SCHED_MIN_NICE (-20)
#define SCHED_MAX_NICE 19

/* All time values (arrival, burst, start, completion, ...) are int64_t ticks.
 *
 * A process is a sequence of phases CPU, I/O, CPU, ..., CPU (an odd count,
 * each > 0) stored in Workload.phases. A plain `pid,arrival,burst` process
 * has the single phase [burst]. */
typedef struct {
    int64_t pid;
    int64_t arrival;
    int64_t burst;    /* total CPU time over all CPU phases */
    int64_t io;       /* total I/O time over all I/O phases */
    int64_t priority; /* lower value = higher priority */
    int64_t tickets;  /* proportional-share weight, 1..SCHED_MAX_TICKETS */
    int64_t nice;     /* CFS nice value, -20..19 */
    size_t phase_start;
    size_t phase_count;
} Process;

typedef struct {
    Process *items;
    size_t len;
    size_t cap;
    int64_t *phases; /* phase pool; each Process owns a contiguous range */
    size_t phases_len;
    size_t phases_cap;
} Workload;

/* Status codes double as process exit codes (see src/cli/main.c). */
typedef enum {
    SCHED_OK = 0,
    SCHED_E_INPUT = 1, /* malformed or invalid workload */
    SCHED_E_USAGE = 2, /* invalid command-line usage or unsupported option combination */
    SCHED_E_IO = 3,    /* a file could not be opened, read or written */
    SCHED_E_NOMEM = 4  /* allocation failure */
} SchedStatus;

enum { SCHED_ERR_MSG_MAX = 256 };

typedef struct {
    char msg[SCHED_ERR_MSG_MAX];
} SchedError;

/* printf-style; err may be NULL. */
void sched_error_set(SchedError *err, const char *fmt, ...) SCHED_PRINTF(2, 3);

/* A process with every optional field at its default. */
Process process_make(int64_t pid, int64_t arrival, int64_t burst);

void workload_init(Workload *w);
void workload_free(Workload *w);

/* Appends a single-CPU-burst process (p.burst is the burst). */
SchedStatus workload_push(Workload *w, Process p, SchedError *err);

/* Appends a process with explicit phases (cpu, io, cpu, ..., cpu); p.burst
 * and p.io are computed from them. */
SchedStatus workload_push_phases(Workload *w, Process p, const int64_t *phases, size_t count,
                                 SchedError *err);

static inline const int64_t *workload_phases(const Workload *w, size_t i) {
    return w->phases + w->items[i].phase_start;
}

static inline bool workload_has_io(const Workload *w) {
    for (size_t i = 0; i < w->len; i++) {
        if (w->items[i].phase_count > 1) return true;
    }
    return false;
}

/* Checks per-process ranges and phases, rejects an empty workload and
 * duplicate PIDs, and guarantees that max(arrival) + sum(every phase) fits
 * in int64_t, which bounds every time value the engine can produce (see
 * sim_run for the extra context-switch allowance). */
SchedStatus workload_validate(const Workload *w, SchedError *err);

/* max(arrival) + sum of every phase of every process: an upper bound on the
 * end of any schedule without context-switch overhead. Requires a validated
 * workload. */
int64_t workload_horizon(const Workload *w);

/* Range checks for a single process's scalar fields; line is used only in
 * the message (0 = omit). */
SchedStatus process_validate(const Process *p, size_t line, SchedError *err);

#endif
