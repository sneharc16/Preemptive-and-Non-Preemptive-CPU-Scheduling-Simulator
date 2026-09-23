/* Table of built-in policies. To add one, write src/policies/<name>.c
 * defining `const Policy POLICY_<NAME>` and add X(POLICY_<NAME>) below.
 * The order here is the order results are printed in. */
#include <string.h>

#include "sched/policy.h"

#define SCHED_POLICIES(X)                                                                          \
    X(POLICY_FCFS)                                                                                 \
    X(POLICY_SJF)                                                                                  \
    X(POLICY_SRTF)                                                                                 \
    X(POLICY_RR)                                                                                   \
    X(POLICY_PRIO)                                                                                 \
    X(POLICY_PRIO_P)                                                                               \
    X(POLICY_HRRN)                                                                                 \
    X(POLICY_MLFQ)                                                                                 \
    X(POLICY_LOTTERY)                                                                              \
    X(POLICY_STRIDE)                                                                               \
    X(POLICY_CFS)                                                                                  \
    X(POLICY_OPT_NP)

#define DECLARE(p) extern const Policy p;
SCHED_POLICIES(DECLARE)
#undef DECLARE

#define ENTRY(p) &p,
static const Policy *const REGISTRY[] = {SCHED_POLICIES(ENTRY)};
#undef ENTRY

_Static_assert(sizeof REGISTRY / sizeof REGISTRY[0] <= SCHED_MAX_POLICIES,
               "raise SCHED_MAX_POLICIES");

size_t policy_count(void) { return sizeof REGISTRY / sizeof REGISTRY[0]; }

const Policy *policy_at(size_t index) { return index < policy_count() ? REGISTRY[index] : NULL; }

const Policy *policy_find(const char *name) {
    for (size_t i = 0; i < policy_count(); i++) {
        if (strcmp(REGISTRY[i]->name, name) == 0) return REGISTRY[i];
    }
    return NULL;
}

void policy_params_default(PolicyParams *p) {
    memset(p, 0, sizeof *p);
    p->quantum = 2;
    p->aging = 0;
    p->mlfq_levels = 3;
    for (size_t l = 0; l < SCHED_MLFQ_MAX_LEVELS; l++) {
        p->mlfq_quanta[l] = p->mlfq_allotment[l] = (int64_t)2 << l; /* 2, 4, 8, ... */
    }
    p->mlfq_boost = 0;
    p->seed = 1;
    p->cfs_latency = 24;
    p->cfs_min_gran = 3;
}
