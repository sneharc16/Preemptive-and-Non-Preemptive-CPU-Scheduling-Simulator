/* Fixed-capacity FIFO ring buffer of process indices. */
#ifndef SCHED_UTIL_QUEUE_H
#define SCHED_UTIL_QUEUE_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    size_t *items;
    size_t cap;
    size_t head;
    size_t len;
} Queue;

bool queue_init(Queue *q, size_t cap); /* false on OOM */
void queue_free(Queue *q);
bool queue_empty(const Queue *q);
void queue_push(Queue *q, size_t v); /* requires len < cap */
size_t queue_pop(Queue *q);          /* requires !queue_empty */

#endif
