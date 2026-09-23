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

/* All time values (arrival, burst, start, completion, ...) are int64_t ticks. */
typedef struct {
    int64_t pid;
    int64_t arrival;
    int64_t burst;
} Process;

typedef struct {
    Process *items;
    size_t len;
    size_t cap;
} Workload;

/* Status codes double as process exit codes (see src/cli/main.c). */
typedef enum {
    SCHED_OK = 0,
    SCHED_E_INPUT = 1, /* malformed or invalid workload */
    SCHED_E_USAGE = 2, /* invalid command-line usage */
    SCHED_E_IO = 3,    /* a file could not be opened, read or written */
    SCHED_E_NOMEM = 4  /* allocation failure */
} SchedStatus;

enum { SCHED_ERR_MSG_MAX = 256 };

typedef struct {
    char msg[SCHED_ERR_MSG_MAX];
} SchedError;

/* printf-style; err may be NULL. */
void sched_error_set(SchedError *err, const char *fmt, ...) SCHED_PRINTF(2, 3);

void workload_init(Workload *w);
void workload_free(Workload *w);
SchedStatus workload_push(Workload *w, Process p, SchedError *err);

/* Checks per-process ranges (pid >= 0, arrival >= 0, burst > 0), rejects an
 * empty workload and duplicate PIDs, and guarantees that
 * max(arrival) + sum(burst) fits in int64_t, which bounds every time value the
 * engine can produce. */
SchedStatus workload_validate(const Workload *w, SchedError *err);

/* Range checks for a single process; line is used only in the message (0 = omit). */
SchedStatus process_validate(const Process *p, size_t line, SchedError *err);

#endif
