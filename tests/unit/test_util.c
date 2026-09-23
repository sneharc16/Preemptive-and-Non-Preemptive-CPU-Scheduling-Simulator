/* Heap, queue and checked-arithmetic unit tests. */
#include "check.h"
#include "sched/checked.h"
#include "util/heap.h"
#include "util/queue.h"

static bool less_by_value(const void *ctx, size_t a, size_t b) {
    const int *keys = ctx;
    return keys[a] != keys[b] ? keys[a] < keys[b] : a < b;
}

static void test_heap_pops_in_order(void) {
    enum { N = 200 };
    int keys[N];
    unsigned seed = 12345;
    for (int i = 0; i < N; i++) {
        seed = seed * 1103515245u + 12345u;
        keys[i] = (int)((seed >> 16) % 50); /* many duplicates exercise the tie-break */
    }
    Heap h;
    CHECK(heap_init(&h, N, less_by_value, keys));
    CHECK(heap_empty(&h));
    for (size_t i = 0; i < N; i++) heap_push(&h, i);
    size_t prev = heap_pop(&h);
    for (int i = 1; i < N; i++) {
        size_t cur = heap_pop(&h);
        CHECK(less_by_value(keys, prev, cur));
        prev = cur;
    }
    CHECK(heap_empty(&h));
    heap_free(&h);
}

static void test_heap_interleaved(void) {
    int keys[] = {5, 3, 8, 1, 9, 2};
    Heap h;
    CHECK(heap_init(&h, 6, less_by_value, keys));
    heap_push(&h, 0);
    heap_push(&h, 1);
    CHECK_EQ_I64(heap_pop(&h), 1); /* key 3 */
    heap_push(&h, 2);
    heap_push(&h, 3);
    CHECK_EQ_I64(heap_pop(&h), 3); /* key 1 */
    heap_push(&h, 4);
    heap_push(&h, 5);
    CHECK_EQ_I64(heap_pop(&h), 5); /* key 2 */
    CHECK_EQ_I64(heap_pop(&h), 0);
    CHECK_EQ_I64(heap_pop(&h), 2);
    CHECK_EQ_I64(heap_pop(&h), 4);
    CHECK(heap_empty(&h));
    heap_free(&h);
}

static void test_queue_fifo_and_wraparound(void) {
    Queue q;
    CHECK(queue_init(&q, 3));
    CHECK(queue_empty(&q));
    queue_push(&q, 10);
    queue_push(&q, 11);
    CHECK_EQ_I64(queue_pop(&q), 10);
    queue_push(&q, 12);
    queue_push(&q, 13); /* wraps around the end of the buffer */
    CHECK_EQ_I64(queue_pop(&q), 11);
    CHECK_EQ_I64(queue_pop(&q), 12);
    queue_push(&q, 14);
    CHECK_EQ_I64(queue_pop(&q), 13);
    CHECK_EQ_I64(queue_pop(&q), 14);
    CHECK(queue_empty(&q));
    queue_free(&q);
}

static void test_checked_add(void) {
    int64_t r = 0;
    CHECK(ck_add_i64(2, 3, &r));
    CHECK_EQ_I64(r, 5);
    CHECK(ck_add_i64(INT64_MAX - 1, 1, &r));
    CHECK_EQ_I64(r, INT64_MAX);
    CHECK(!ck_add_i64(INT64_MAX, 1, &r));
    CHECK_EQ_I64(r, INT64_MAX); /* untouched on failure */
    CHECK(!ck_add_i64(INT64_MIN, -1, &r));
    CHECK(ck_add_i64(INT64_MIN, INT64_MAX, &r));
    CHECK_EQ_I64(r, -1);
}

static void test_checked_sub(void) {
    int64_t r = 0;
    CHECK(ck_sub_i64(10, 4, &r));
    CHECK_EQ_I64(r, 6);
    CHECK(!ck_sub_i64(INT64_MIN, 1, &r));
    CHECK(!ck_sub_i64(INT64_MAX, -1, &r));
    CHECK(ck_sub_i64(-1, INT64_MAX, &r));
    CHECK_EQ_I64(r, INT64_MIN);
}

int main(void) {
    RUN_TEST(test_heap_pops_in_order);
    RUN_TEST(test_heap_interleaved);
    RUN_TEST(test_queue_fifo_and_wraparound);
    RUN_TEST(test_checked_add);
    RUN_TEST(test_checked_sub);
    return CHECK_SUMMARY();
}
