/* libFuzzer harness for the input parsers and, through them, the engine.
 *
 * Byte 0 selects the input format and machine options; the rest is the
 * workload text. Anything that parses into a small valid workload is then
 * simulated under every online policy (and opt-np when small enough), and
 * each schedule is checked for basic invariants; a violation aborts, which
 * the fuzzer reports as a crash. Build with -fsanitize=fuzzer (see the fuzz
 * job in .github/workflows/ci.yml) or link tests/fuzz/replay.c to run saved
 * inputs without libFuzzer.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sched/engine.h"
#include "sched/io.h"

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "invariant failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__);          \
            abort();                                                                               \
        }                                                                                          \
    } while (0)

static void check_schedule(const Workload *w, const SimResult *r) {
    int64_t *ran = calloc(w->len, sizeof *ran);
    CHECK(ran != NULL);
    for (size_t c = 0; c < r->cores; c++) {
        const Timeline *t = &r->timelines[c];
        CHECK(t->len > 0 && t->items[0].start == r->first_arrival);
        for (size_t i = 0; i < t->len; i++) {
            const Segment *s = &t->items[i];
            CHECK(s->start < s->end);
            if (i + 1 < t->len) CHECK(s->end == t->items[i + 1].start);
            if (s->kind == SEG_RUN) ran[s->proc] += s->end - s->start;
            if (s->kind == SEG_CS) CHECK(s->end - s->start == r->options.cs_cost);
        }
    }
    for (size_t i = 0; i < w->len; i++) {
        const Process *p = &w->items[i];
        CHECK(ran[i] == p->burst);
        CHECK(r->outcomes[i].start >= p->arrival);
        CHECK(r->outcomes[i].completion >= p->arrival + p->burst + p->io);
    }
    free(ran);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 2) return 0;
    uint8_t mode = data[0];
    FILE *in = fmemopen((void *)(data + 1), size - 1, "r");
    if (!in) return 0;
    Workload w;
    workload_init(&w);
    SchedError err;
    SchedStatus st = (mode & 1) ? read_workload_csv(in, &w, &err)
                                : read_workload_interactive(in, NULL, &w, &err);
    fclose(in);
    if (st == SCHED_OK) st = workload_validate(&w, &err);
    /* Keep simulations small so each input runs in well under a millisecond. */
    if (st == SCHED_OK && w.len <= 32 && workload_horizon(&w) <= 20000) {
        PolicyParams params;
        policy_params_default(&params);
        params.quantum = 1 + ((mode >> 6) & 3);
        params.aging = (mode >> 5) & 1 ? 3 : 0;
        params.mlfq_boost = (mode >> 5) & 1 ? 25 : 0;
        SimOptions opt;
        sim_options_default(&opt);
        opt.cores = 1 + ((mode >> 1) & 3) % 3;
        opt.cs_cost = (mode >> 3) & 3;
        opt.predict = (mode >> 5) & 1 ? PREDICT_EWMA : PREDICT_ORACLE;
        opt.max_segments = 1 << 14;
        for (size_t i = 0; i < policy_count(); i++) {
            const Policy *policy = policy_at(i);
            if (policy->offline && (w.len > 8 || opt.cores != 1 || opt.cs_cost != 0)) continue;
            SimResult r;
            if (sim_run(&w, policy, &params, &opt, &r, &err) == SCHED_OK) {
                check_schedule(&w, &r);
                sim_result_free(&r);
            }
        }
    }
    workload_free(&w);
    return 0;
}
