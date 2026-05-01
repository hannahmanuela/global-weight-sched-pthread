#pragma once
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// Lock-free MPMC ring buffer (Vyukov, 2010).
// Fixed capacity; no malloc. QUEUE_CAPACITY must be a power of 2.

#ifndef QUEUE_CAPACITY
#define QUEUE_CAPACITY 1024
#endif

typedef struct {
    _Atomic size_t  seq;
    void           *val;
} queue_slot_t;

typedef struct {
    _Alignas(64) _Atomic size_t head;
    _Alignas(64) _Atomic size_t tail;
    queue_slot_t slots[QUEUE_CAPACITY];
} queue_t;

static inline void queue_init(queue_t *q) {
    atomic_store(&q->head, 0);
    atomic_store(&q->tail, 0);
    for (size_t i = 0; i < QUEUE_CAPACITY; i++)
        atomic_store(&q->slots[i].seq, i);
}

// Returns false if queue is full.
static inline bool queue_push(queue_t *q, void *val) {
    size_t pos = atomic_load_explicit(&q->tail, memory_order_relaxed);
    for (;;) {
        queue_slot_t *slot = &q->slots[pos & (QUEUE_CAPACITY - 1)];
        size_t seq = atomic_load_explicit(&slot->seq, memory_order_acquire);
        intptr_t diff = (intptr_t)seq - (intptr_t)pos;
        if (diff == 0) {
            if (atomic_compare_exchange_weak_explicit(
                    &q->tail, &pos, pos + 1,
                    memory_order_relaxed, memory_order_relaxed)) {
                slot->val = val;
                atomic_store_explicit(&slot->seq, pos + 1, memory_order_release);
                return true;
            }
        } else if (diff < 0) {
            return false;  // full
        } else {
            pos = atomic_load_explicit(&q->tail, memory_order_relaxed);
        }
    }
}

// Returns NULL if empty.
static inline void *queue_pop(queue_t *q) {
    size_t pos = atomic_load_explicit(&q->head, memory_order_relaxed);
    for (;;) {
        queue_slot_t *slot = &q->slots[pos & (QUEUE_CAPACITY - 1)];
        size_t seq = atomic_load_explicit(&slot->seq, memory_order_acquire);
        intptr_t diff = (intptr_t)seq - (intptr_t)(pos + 1);
        if (diff == 0) {
            if (atomic_compare_exchange_weak_explicit(
                    &q->head, &pos, pos + 1,
                    memory_order_relaxed, memory_order_relaxed)) {
                void *val = slot->val;
                atomic_store_explicit(&slot->seq, pos + QUEUE_CAPACITY, memory_order_release);
                return val;
            }
        } else if (diff < 0) {
            return NULL;  // empty
        } else {
            pos = atomic_load_explicit(&q->head, memory_order_relaxed);
        }
    }
}
