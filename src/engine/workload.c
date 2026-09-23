#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "sched/checked.h"
#include "sched/core.h"

void sched_error_set(SchedError *err, const char *fmt, ...) {
    if (!err) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err->msg, sizeof err->msg, fmt, ap);
    va_end(ap);
}

void workload_init(Workload *w) {
    w->items = NULL;
    w->len = w->cap = 0;
}

void workload_free(Workload *w) {
    free(w->items);
    workload_init(w);
}

SchedStatus workload_push(Workload *w, Process p, SchedError *err) {
    if (w->len == w->cap) {
        size_t ncap = w->cap ? w->cap * 2 : 16;
        Process *items = NULL;
        if (ncap > w->cap && ncap <= SIZE_MAX / sizeof *items) {
            items = realloc(w->items, ncap * sizeof *items);
        }
        if (!items) {
            sched_error_set(err, "out of memory while reading %zu processes", w->len + 1);
            return SCHED_E_NOMEM;
        }
        w->items = items;
        w->cap = ncap;
    }
    w->items[w->len++] = p;
    return SCHED_OK;
}

static void line_prefix(char *buf, size_t size, size_t line) {
    if (line) {
        snprintf(buf, size, "line %zu: ", line);
    } else {
        buf[0] = '\0';
    }
}

SchedStatus process_validate(const Process *p, size_t line, SchedError *err) {
    char at[32];
    line_prefix(at, sizeof at, line);
    if (p->pid < 0) {
        sched_error_set(err, "%sPID must be >= 0 (got %" PRId64 ")", at, p->pid);
        return SCHED_E_INPUT;
    }
    if (p->arrival < 0) {
        sched_error_set(err, "%sarrival of P%" PRId64 " must be >= 0 (got %" PRId64 ")", at, p->pid,
                        p->arrival);
        return SCHED_E_INPUT;
    }
    if (p->burst <= 0) {
        sched_error_set(err, "%sburst of P%" PRId64 " must be > 0 (got %" PRId64 ")", at, p->pid,
                        p->burst);
        return SCHED_E_INPUT;
    }
    return SCHED_OK;
}

static int cmp_i64(const void *a, const void *b) {
    int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;
    return (x > y) - (x < y);
}

SchedStatus workload_validate(const Workload *w, SchedError *err) {
    if (w->len == 0) {
        sched_error_set(err, "workload is empty: at least one process is required");
        return SCHED_E_INPUT;
    }
    int64_t max_arrival = 0, total_burst = 0, horizon;
    for (size_t i = 0; i < w->len; i++) {
        SchedStatus st = process_validate(&w->items[i], 0, err);
        if (st != SCHED_OK) return st;
        if (w->items[i].arrival > max_arrival) max_arrival = w->items[i].arrival;
        if (!ck_add_i64(total_burst, w->items[i].burst, &total_burst)) {
            sched_error_set(err, "sum of burst times overflows a 64-bit tick counter");
            return SCHED_E_INPUT;
        }
    }
    if (!ck_add_i64(max_arrival, total_burst, &horizon)) {
        sched_error_set(err, "latest arrival + total burst time overflows a 64-bit tick counter");
        return SCHED_E_INPUT;
    }

    int64_t *pids = malloc(w->len * sizeof *pids);
    if (!pids) {
        sched_error_set(err, "out of memory while validating %zu processes", w->len);
        return SCHED_E_NOMEM;
    }
    for (size_t i = 0; i < w->len; i++) pids[i] = w->items[i].pid;
    qsort(pids, w->len, sizeof *pids, cmp_i64);
    SchedStatus st = SCHED_OK;
    for (size_t i = 1; i < w->len; i++) {
        if (pids[i] == pids[i - 1]) {
            sched_error_set(err, "duplicate PID %" PRId64 ": every process needs a unique PID",
                            pids[i]);
            st = SCHED_E_INPUT;
            break;
        }
    }
    free(pids);
    return st;
}
