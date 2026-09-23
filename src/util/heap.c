#include "util/heap.h"

#include <assert.h>
#include <stdlib.h>

bool heap_init(Heap *h, size_t cap, HeapLess less, const void *ctx) {
    h->items = malloc((cap ? cap : 1) * sizeof *h->items);
    h->len = 0;
    h->cap = cap;
    h->less = less;
    h->ctx = ctx;
    return h->items != NULL;
}

void heap_free(Heap *h) {
    free(h->items);
    h->items = NULL;
    h->len = h->cap = 0;
}

bool heap_empty(const Heap *h) { return h->len == 0; }

static void swap(size_t *a, size_t *b) {
    size_t t = *a;
    *a = *b;
    *b = t;
}

void heap_push(Heap *h, size_t v) {
    assert(h->len < h->cap);
    size_t i = h->len++;
    h->items[i] = v;
    while (i > 0) {
        size_t parent = (i - 1) / 2;
        if (!h->less(h->ctx, h->items[i], h->items[parent])) break;
        swap(&h->items[i], &h->items[parent]);
        i = parent;
    }
}

size_t heap_pop(Heap *h) {
    assert(h->len > 0);
    size_t top = h->items[0];
    h->items[0] = h->items[--h->len];
    size_t i = 0;
    for (;;) {
        size_t l = 2 * i + 1, r = l + 1, m = i;
        if (l < h->len && h->less(h->ctx, h->items[l], h->items[m])) m = l;
        if (r < h->len && h->less(h->ctx, h->items[r], h->items[m])) m = r;
        if (m == i) break;
        swap(&h->items[i], &h->items[m]);
        i = m;
    }
    return top;
}
