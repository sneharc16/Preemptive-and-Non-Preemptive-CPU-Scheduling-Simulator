/* Fixed-capacity binary min-heap of process indices with a caller-supplied
 * strict ordering. Capacity is set once; policies size it to the workload,
 * since a process is never in the ready set twice. */
#ifndef SCHED_UTIL_HEAP_H
#define SCHED_UTIL_HEAP_H

#include <stdbool.h>
#include <stddef.h>

typedef bool (*HeapLess)(const void *ctx, size_t a, size_t b);

typedef struct {
    size_t *items;
    size_t len;
    size_t cap;
    HeapLess less;
    const void *ctx;
} Heap;

bool heap_init(Heap *h, size_t cap, HeapLess less, const void *ctx); /* false on OOM */
void heap_free(Heap *h);
bool heap_empty(const Heap *h);
void heap_push(Heap *h, size_t v); /* requires len < cap */
size_t heap_pop(Heap *h);          /* requires !heap_empty */

#endif
