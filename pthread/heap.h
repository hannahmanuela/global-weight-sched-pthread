#ifndef _HEAP_H_

#define _HEAP_H_

#include "util.h"
#include "heap_elem.h"
#include "lock.h"

#define HEAP_CAPACITY 1024    // XXX todo: reallocating while running mh_min_atomic

struct heap {
	struct spinlock lk __calign__;

	// vt_t min_vt; //  __calign__;

	struct heap_elem *heap[HEAP_CAPACITY] __calign__;
	// struct heap_elem *heap __calign__;

	int heap_capacity;
	int id;
	int max;

	int heap_size __calign__;
	vt_t last_vt;

	is_lt_elem_t lt;  // ordering used for sift up/down; must match the scheduler's is_lt_elem
}  __calign__;

typedef void (*heap_iter_t)(struct heap_elem *);

struct heap *heap_new(is_lt_elem_t lt);
void heap_free(struct heap *h);
struct heap_elem *heap_min(struct heap *h);
void heap_push(struct heap *h, struct heap_elem *e);
struct heap_elem *heap_remove_min(struct heap *h);
bool heap_erase(struct heap *h, struct heap_elem *e);
void heap_iter(struct heap *h, heap_iter_t);

void heap_elem_init(struct heap_elem *he, vt_t vt, int w);

#endif
