#include "util/queue.h"

#include <assert.h>
#include <stdlib.h>

bool queue_init(Queue *q, size_t cap) {
    q->items = malloc((cap ? cap : 1) * sizeof *q->items);
    q->cap = cap;
    q->head = q->len = 0;
    return q->items != NULL;
}

void queue_free(Queue *q) {
    free(q->items);
    q->items = NULL;
    q->cap = q->head = q->len = 0;
}

bool queue_empty(const Queue *q) { return q->len == 0; }

void queue_push(Queue *q, size_t v) {
    assert(q->len < q->cap);
    q->items[(q->head + q->len) % q->cap] = v;
    q->len++;
}

size_t queue_pop(Queue *q) {
    assert(q->len > 0);
    size_t v = q->items[q->head];
    q->head = (q->head + 1) % q->cap;
    q->len--;
    return v;
}
