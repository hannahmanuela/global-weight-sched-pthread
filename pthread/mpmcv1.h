#ifndef _MPMCV1_H_

#define _MPMCV1_H_

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// Lock-free MPMC ring buffer (Vyukov, 2010).
// Fixed capacity; no malloc. QUEUE_CAPACITY must be a power of 2.
// Using turn design

#ifndef QUEUE_CAPACITY
#define QUEUE_CAPACITY 1024
#endif

typedef struct {
    _Atomic size_t  turn;
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
        atomic_store(&q->slots[i].turn, 0);
}

#define TURN(s) ((s)/QUEUE_CAPACITY)

// Returns false if queue is full.
static inline bool queue_push(queue_t *q, void *val) {
	size_t pos = atomic_load_explicit(&q->tail, memory_order_acquire);
	for (;;) {
		queue_slot_t *slot = &q->slots[pos & (QUEUE_CAPACITY - 1)];
		size_t turn = atomic_load_explicit(&slot->turn, memory_order_acquire);
		if (TURN(pos)*2 == turn) {
			if (atomic_compare_exchange_weak_explicit(
				    &q->tail, &pos, pos + 1,
				    memory_order_acquire, memory_order_acquire)) {
				slot->val = val;
				atomic_store_explicit(&slot->turn, TURN(pos)*2+1, memory_order_release);
				return true;
			}
		} else {
			size_t prev_tail = pos;
			pos = atomic_load_explicit(&q->tail, memory_order_acquire);
			if (pos == prev_tail) {
				size_t head = atomic_load_explicit(&q->head, memory_order_acquire);
				// printf("push: %d %d %d %d\n", pos, prev_tail, head, pos-head);
				
				return false;
			}
		}
	}
}

// Returns NULL if empty.
static inline void *queue_pop(queue_t *q) {
	size_t pos = atomic_load_explicit(&q->head, memory_order_acquire);
	for (;;) {
		queue_slot_t *slot = &q->slots[pos & (QUEUE_CAPACITY - 1)];
		size_t turn = atomic_load_explicit(&slot->turn, memory_order_acquire);
		if (TURN(pos)*2+1 == turn) {
			if (atomic_compare_exchange_weak_explicit(
				    &q->head, &pos, pos + 1,
				    memory_order_acquire, memory_order_acquire)) {
				void *val = slot->val;
				atomic_store_explicit(&slot->turn, TURN(pos)*2+2, memory_order_release);
				return val;
			}
		}  else {
			size_t prev_head = pos;
			pos = atomic_load_explicit(&q->head, memory_order_acquire);
			if (pos == prev_head) {
				return NULL;
			}
		}
	}
}

#endif
