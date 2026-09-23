/* CFS-lite: a simplified model of the Linux Completely Fair Scheduler.
 * It captures CFS's core idea, not its implementation.
 *
 *   - Weights come from the Linux nice-to-weight table (nice 0 = 1024).
 *   - vruntime advances by ran * 1024 / weight, in fixed point with 2^16
 *     units per nice-0 tick: delta = floor(ran * 2^26 / weight).
 *   - The ready process with the smallest (vruntime, pid) runs next, for
 *     max(floor(latency * weight / W), min_gran) ticks, where W is the total
 *     weight of runnable processes (ready or running).
 *   - min_vruntime follows the leftmost queued process and never decreases.
 *     New processes start at min_vruntime; a process waking from I/O gets
 *     max(own vruntime, min_vruntime - latency/2) (a sleeper credit).
 *
 * Not modelled: wakeup preemption, the red-black tree's cache and group
 * scheduling, per-CPU run queues and load balancing, START_DEBIT for new
 * tasks, and the sched_nr_latency period stretch.
 */
#include <inttypes.h>
#include <stdlib.h>

#include "sched/policy.h"
#include "util/heap.h"

#define NICE0_SHIFT 16                       /* vruntime units per nice-0 tick = 2^16 */
#define CFS_MAX_CPU ((int64_t)1000000000000) /* keeps vruntime within int64 */

/* Linux kernel/sched/core.c sched_prio_to_weight[], index nice + 20. */
static const int64_t NICE_TO_WEIGHT[40] = {
    88761, 71755, 56483, 46273, 36291, 29154, 23254, 18705, 14949, 11916, 9548, 7620, 6100, 4904,
    3906,  3121,  2501,  1991,  1586,  1277,  1024,  820,   655,   526,   423,  335,  272,  215,
    172,   137,   110,   87,    70,    56,    45,    36,    29,    23,    18,   15,
};

typedef struct {
    const SchedView *view;
    int64_t latency, min_gran;
    int64_t *vruntime;
    int64_t min_vruntime;
    int64_t runnable_weight;
    Heap ready;
} Cfs;

static int64_t weight(const Cfs *s, size_t p) {
    return NICE_TO_WEIGHT[s->view->procs[p].nice + 20];
}

/* floor(ran * 2^26 / w) without overflowing for ran up to CFS_MAX_CPU. */
static int64_t vruntime_delta(int64_t ran, int64_t w) {
    const int shift = NICE0_SHIFT + 10;
    return ((ran / w) << shift) + (((ran % w) << shift) / w);
}

static bool cfs_less(const void *ctx, size_t a, size_t b) {
    const Cfs *s = ctx;
    if (s->vruntime[a] != s->vruntime[b]) return s->vruntime[a] < s->vruntime[b];
    return s->view->procs[a].pid < s->view->procs[b].pid;
}

static void update_min_vruntime(Cfs *s) {
    if (heap_empty(&s->ready)) return;
    int64_t left = s->vruntime[s->ready.items[0]];
    if (left > s->min_vruntime) s->min_vruntime = left;
}

static void *cfs_create(const SchedView *view, const PolicyParams *params, SchedError *err) {
    int64_t total = 0;
    for (size_t i = 0; i < view->n; i++) total += view->procs[i].burst;
    if (total > CFS_MAX_CPU || params->cfs_latency > 1000000000) {
        sched_error_set(
            err, "cfs: total CPU time must be <= %" PRId64 " ticks and --cfs-latency <= 1000000000",
            CFS_MAX_CPU);
        return NULL;
    }
    Cfs *s = calloc(1, sizeof *s);
    if (s) s->vruntime = calloc(view->n, sizeof *s->vruntime);
    if (!s || !s->vruntime || !heap_init(&s->ready, view->n, cfs_less, s)) {
        if (s) free(s->vruntime);
        free(s);
        sched_error_set(err, "out of memory");
        return NULL;
    }
    s->view = view;
    s->latency = params->cfs_latency;
    s->min_gran = params->cfs_min_gran;
    return s;
}

static void cfs_destroy(void *self) {
    Cfs *s = self;
    heap_free(&s->ready);
    free(s->vruntime);
    free(s);
}

static void enqueue(Cfs *s, size_t p) {
    heap_push(&s->ready, p);
    update_min_vruntime(s);
}

static void cfs_arrival(void *self, size_t p) {
    Cfs *s = self;
    s->vruntime[p] = s->min_vruntime;
    s->runnable_weight += weight(s, p);
    enqueue(s, p);
}

static void cfs_ready(void *self, size_t p) {
    Cfs *s = self;
    int64_t floor = s->min_vruntime - ((s->latency << NICE0_SHIFT) / 2);
    if (s->vruntime[p] < floor) s->vruntime[p] = floor;
    s->runnable_weight += weight(s, p);
    enqueue(s, p);
}

static void cfs_preempt(void *self, size_t p, int64_t ran) {
    Cfs *s = self;
    s->vruntime[p] += vruntime_delta(ran, weight(s, p));
    enqueue(s, p);
}

static void cfs_leave(void *self, size_t p, int64_t ran) {
    Cfs *s = self;
    s->vruntime[p] += vruntime_delta(ran, weight(s, p));
    s->runnable_weight -= weight(s, p);
}

static bool cfs_next_slice(void *self, Slice *out) {
    Cfs *s = self;
    if (heap_empty(&s->ready)) return false;
    size_t p = heap_pop(&s->ready);
    update_min_vruntime(s);
    int64_t slice = s->latency * weight(s, p) / s->runnable_weight;
    out->proc = p;
    out->length = slice > s->min_gran ? slice : s->min_gran;
    out->deadline = SCHED_NO_DEADLINE;
    return true;
}

const Policy POLICY_CFS = {
    .name = "cfs",
    .label = "CFS",
    .heading = "CFS-lite (Completely Fair Scheduler model) Scheduling",
    .preemptive = true,
    .preempt_on_ready = false,
    .uses_quantum = false,
    .uses_estimates = false,
    .work_conserving = true,
    .create = cfs_create,
    .destroy = cfs_destroy,
    .on_arrival = cfs_arrival,
    .on_ready = cfs_ready,
    .next_slice = cfs_next_slice,
    .on_preempt = cfs_preempt,
    .on_block = cfs_leave,
    .on_complete = cfs_leave,
};
