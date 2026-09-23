/* Minimal header-only unit-test helpers. Each test binary defines its test
 * functions, calls RUN_TEST for each from main, and returns CHECK_SUMMARY(). */
#ifndef SCHED_TEST_CHECK_H
#define SCHED_TEST_CHECK_H

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sched/core.h"

static int check_failures;
static int check_total;
static int check_tests;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        check_total++;                                                                             \
        if (!(cond)) {                                                                             \
            check_failures++;                                                                      \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);               \
        }                                                                                          \
    } while (0)

#define CHECK_EQ_I64(actual, expected)                                                             \
    do {                                                                                           \
        int64_t check_a_ = (int64_t)(actual), check_e_ = (int64_t)(expected);                      \
        check_total++;                                                                             \
        if (check_a_ != check_e_) {                                                                \
            check_failures++;                                                                      \
            fprintf(stderr, "%s:%d: %s == %" PRId64 ", expected %" PRId64 "\n", __FILE__,          \
                    __LINE__, #actual, check_a_, check_e_);                                        \
        }                                                                                          \
    } while (0)

#define CHECK_STR_EQ(actual, expected)                                                             \
    do {                                                                                           \
        const char *check_a_ = (actual), *check_e_ = (expected);                                   \
        check_total++;                                                                             \
        if (strcmp(check_a_, check_e_) != 0) {                                                     \
            check_failures++;                                                                      \
            fprintf(stderr, "%s:%d: %s\n  got:      \"%s\"\n  expected: \"%s\"\n", __FILE__,       \
                    __LINE__, #actual, check_a_, check_e_);                                        \
        }                                                                                          \
    } while (0)

#define CHECK_CONTAINS(haystack, needle)                                                           \
    do {                                                                                           \
        const char *check_h_ = (haystack), *check_n_ = (needle);                                   \
        check_total++;                                                                             \
        if (!strstr(check_h_, check_n_)) {                                                         \
            check_failures++;                                                                      \
            fprintf(stderr, "%s:%d: \"%s\" does not contain \"%s\"\n", __FILE__, __LINE__,         \
                    check_h_, check_n_);                                                           \
        }                                                                                          \
    } while (0)

#define RUN_TEST(fn)                                                                               \
    do {                                                                                           \
        int check_before_ = check_failures;                                                        \
        check_tests++;                                                                             \
        fn();                                                                                      \
        if (check_failures != check_before_) fprintf(stderr, "FAIL %s\n", #fn);                    \
    } while (0)

#define CHECK_SUMMARY()                                                                            \
    (printf("%d tests, %d checks, %d failures\n", check_tests, check_total, check_failures),       \
     check_failures ? EXIT_FAILURE : EXIT_SUCCESS)

/* Returns a temporary stream positioned at the start of `text`. */
static inline FILE *stream_from(const char *text) {
    FILE *f = tmpfile();
    if (!f) {
        perror("tmpfile");
        exit(EXIT_FAILURE);
    }
    fputs(text, f);
    rewind(f);
    return f;
}

/* Reads a whole stream (from the start) into a malloc'd string. */
static inline char *stream_contents(FILE *f) {
    fflush(f);
    long size;
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0) exit(EXIT_FAILURE);
    rewind(f);
    char *buf = malloc((size_t)size + 1);
    if (!buf || fread(buf, 1, (size_t)size, f) != (size_t)size) exit(EXIT_FAILURE);
    buf[size] = '\0';
    return buf;
}

/* (pid, arrival, burst) with every optional field at its default. */
typedef struct {
    int64_t pid, arrival, burst;
} Triple;

static inline Workload workload_of(const Triple *t, size_t n) {
    Workload w;
    workload_init(&w);
    for (size_t i = 0; i < n; i++) {
        if (workload_push(&w, process_make(t[i].pid, t[i].arrival, t[i].burst), NULL) != SCHED_OK)
            exit(EXIT_FAILURE);
    }
    return w;
}

#endif
