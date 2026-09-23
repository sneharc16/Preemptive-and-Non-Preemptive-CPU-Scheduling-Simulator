#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sched/checked.h"
#include "sched/core.h"

void sched_error_set(SchedError *err, const char *fmt, ...) {
    if (!err) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err->msg, sizeof err->msg, fmt, ap);
    va_end(ap);
}

Process process_make(int64_t pid, int64_t arrival, int64_t burst) {
    return (Process){.pid = pid,
                     .arrival = arrival,
                     .burst = burst,
                     .priority = SCHED_DEFAULT_PRIORITY,
                     .tickets = SCHED_DEFAULT_TICKETS,
                     .nice = SCHED_DEFAULT_NICE};
}

void workload_init(Workload *w) { memset(w, 0, sizeof *w); }

void workload_free(Workload *w) {
    free(w->items);
    free(w->phases);
    workload_init(w);
}

/* Grows *items (element size `size`) so that it can hold `need` elements. */
static bool grow(void **items, size_t *cap, size_t need, size_t size) {
    if (need <= *cap) return true;
    size_t ncap = *cap ? *cap : 16;
    while (ncap < need) {
        if (ncap > SIZE_MAX / 2) return false;
        ncap *= 2;
    }
    if (ncap > SIZE_MAX / size) return false;
    void *p = realloc(*items, ncap * size);
    if (!p) return false;
    *items = p;
    *cap = ncap;
    return true;
}

SchedStatus workload_push_phases(Workload *w, Process p, const int64_t *phases, size_t count,
                                 SchedError *err) {
    if (count == 0 || count % 2 == 0) {
        sched_error_set(err, "P%" PRId64 ": needs an odd number of phases (cpu:io:...:cpu)", p.pid);
        return SCHED_E_INPUT;
    }
    int64_t cpu = 0, io = 0;
    for (size_t k = 0; k < count; k++) {
        if (phases[k] <= 0) {
            sched_error_set(err, "P%" PRId64 ": every burst must be > 0 (phase %zu is %" PRId64 ")",
                            p.pid, k + 1, phases[k]);
            return SCHED_E_INPUT;
        }
        int64_t *sum = k % 2 == 0 ? &cpu : &io;
        if (!ck_add_i64(*sum, phases[k], sum)) {
            sched_error_set(err, "P%" PRId64 ": total burst time overflows a 64-bit tick counter",
                            p.pid);
            return SCHED_E_INPUT;
        }
    }
    if (w->phases_len > SIZE_MAX - count || w->len == SIZE_MAX ||
        !grow((void **)&w->phases, &w->phases_cap, w->phases_len + count, sizeof *w->phases) ||
        !grow((void **)&w->items, &w->cap, w->len + 1, sizeof *w->items)) {
        sched_error_set(err, "out of memory while reading %zu processes", w->len + 1);
        return SCHED_E_NOMEM;
    }
    memcpy(w->phases + w->phases_len, phases, count * sizeof *phases);
    p.burst = cpu;
    p.io = io;
    p.phase_start = w->phases_len;
    p.phase_count = count;
    w->phases_len += count;
    w->items[w->len++] = p;
    return SCHED_OK;
}

SchedStatus workload_push(Workload *w, Process p, SchedError *err) {
    return workload_push_phases(w, p, &p.burst, 1, err);
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
    if (p->tickets < 1 || p->tickets > SCHED_MAX_TICKETS) {
        sched_error_set(err,
                        "%stickets of P%" PRId64 " must be in 1..%" PRId64 " (got %" PRId64 ")", at,
                        p->pid, SCHED_MAX_TICKETS, p->tickets);
        return SCHED_E_INPUT;
    }
    if (p->nice < SCHED_MIN_NICE || p->nice > SCHED_MAX_NICE) {
        sched_error_set(err, "%snice of P%" PRId64 " must be in -20..19 (got %" PRId64 ")", at,
                        p->pid, p->nice);
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
    int64_t max_arrival = 0, total = 0, horizon;
    for (size_t i = 0; i < w->len; i++) {
        const Process *p = &w->items[i];
        SchedStatus st = process_validate(p, 0, err);
        if (st != SCHED_OK) return st;
        if (p->arrival > max_arrival) max_arrival = p->arrival;
        int64_t cpu = 0, io = 0;
        const int64_t *ph = workload_phases(w, i);
        for (size_t k = 0; k < p->phase_count; k++) {
            if (ph[k] <= 0 || !ck_add_i64(k % 2 ? io : cpu, ph[k], k % 2 ? &io : &cpu) ||
                !ck_add_i64(total, ph[k], &total)) {
                sched_error_set(err, "sum of burst times overflows a 64-bit tick counter");
                return SCHED_E_INPUT;
            }
        }
        if (p->phase_count % 2 == 0 || cpu != p->burst || io != p->io) {
            sched_error_set(err, "P%" PRId64 ": inconsistent burst phases", p->pid);
            return SCHED_E_INPUT;
        }
    }
    if (!ck_add_i64(max_arrival, total, &horizon)) {
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

int64_t workload_horizon(const Workload *w) {
    int64_t max_arrival = 0, total = 0;
    for (size_t i = 0; i < w->len; i++) {
        if (w->items[i].arrival > max_arrival) max_arrival = w->items[i].arrival;
        total += w->items[i].burst + w->items[i].io;
    }
    return max_arrival + total;
}
