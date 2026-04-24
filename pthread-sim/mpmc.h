#pragma once
#include <stdatomic.h>
#include <stdlib.h>

// Lock-free MPMC queue (Michael-Scott, 1996).
// Linearizable, ABA-safe via counted pointers.

typedef struct queue_node {
    void *val;
    _Atomic(struct queue_node *) next;
} queue_node_t;

typedef struct {
    _Alignas(64) _Atomic(queue_node_t *) head;
    _Alignas(64) _Atomic(queue_node_t *) tail;
} queue_t;

static inline void queue_init(queue_t *q) {
    queue_node_t *dummy = malloc(sizeof(*dummy));
    dummy->val = NULL;
    atomic_store(&dummy->next, NULL);
    atomic_store(&q->head, dummy);
    atomic_store(&q->tail, dummy);
}

static inline void queue_push(queue_t *q, void *val) {
    queue_node_t *node = malloc(sizeof(*node));
    node->val = val;
    atomic_store_explicit(&node->next, NULL, memory_order_relaxed);

    queue_node_t *tail, *next;
    for (;;) {
        tail = atomic_load_explicit(&q->tail, memory_order_acquire);
        next = atomic_load_explicit(&tail->next, memory_order_acquire);
        if (tail != atomic_load_explicit(&q->tail, memory_order_acquire))
            continue;
        if (next == NULL) {
            if (atomic_compare_exchange_weak_explicit(
                    &tail->next, &next, node,
                    memory_order_release, memory_order_relaxed))
                break;
        } else {
            // help swing tail forward
            atomic_compare_exchange_weak_explicit(
                &q->tail, &tail, next,
                memory_order_release, memory_order_relaxed);
        }
    }
    atomic_compare_exchange_weak_explicit(
        &q->tail, &tail, node,
        memory_order_release, memory_order_relaxed);
}

// Returns NULL if empty.
static inline void *queue_pop(queue_t *q) {
    queue_node_t *head, *tail, *next;
    for (;;) {
        head = atomic_load_explicit(&q->head, memory_order_acquire);
        tail = atomic_load_explicit(&q->tail, memory_order_acquire);
        next = atomic_load_explicit(&head->next, memory_order_acquire);
        if (head != atomic_load_explicit(&q->head, memory_order_acquire))
            continue;
        if (head == tail) {
            if (next == NULL)
                return NULL;  // empty
            // tail is lagging; help it along
            atomic_compare_exchange_weak_explicit(
                &q->tail, &tail, next,
                memory_order_release, memory_order_relaxed);
        } else {
            void *val = next->val;
            if (atomic_compare_exchange_weak_explicit(
                    &q->head, &head, next,
                    memory_order_release, memory_order_relaxed)) {
                free(head);  // old dummy
                return val;
            }
        }
    }
}
