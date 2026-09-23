/* Table of built-in policies. To add one, write src/policies/<name>.c
 * defining `const Policy POLICY_<NAME>` and add X(POLICY_<NAME>) below.
 * The order here is the order results are printed in. */
#include <string.h>

#include "sched/policy.h"

#define SCHED_POLICIES(X)                                                                          \
    X(POLICY_FCFS)                                                                                 \
    X(POLICY_SJF)                                                                                  \
    X(POLICY_SRTF)                                                                                 \
    X(POLICY_RR)

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
