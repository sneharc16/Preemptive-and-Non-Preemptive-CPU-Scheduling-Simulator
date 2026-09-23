/* Overflow-checked int64 arithmetic. Each helper returns false (and leaves
 * *out untouched) when the exact result is not representable. */
#ifndef SCHED_CHECKED_H
#define SCHED_CHECKED_H

#include <stdbool.h>
#include <stdint.h>

static inline bool ck_add_i64(int64_t a, int64_t b, int64_t *out) {
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) {
        return false;
    }
    *out = a + b;
    return true;
}

static inline bool ck_sub_i64(int64_t a, int64_t b, int64_t *out) {
    if ((b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b)) {
        return false;
    }
    *out = a - b;
    return true;
}

#endif
