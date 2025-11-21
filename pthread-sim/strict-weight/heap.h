#ifndef _HEAP_H_

#define _HEAP_H_

#include "util.h"
#include "heap_elem.h"
#include "lock.h"

#define HEAP_CAPACITY 64    // XXX todo: reallocating while running mh_min_atomic

struct heap {
	struct spinlock lk __calign__;

	// vt_t min_vt; //  __calign__;

	struct heap_elem heap[HEAP_CAPACITY] __calign__;
	// struct heap_elem *heap __calign__;

	cmp_elem_t cmp_elem;
	int heap_capacity;
	int id;

	int heap_size __calign__;
}  __calign__;

typedef void (*heap_iter_t)(struct heap_elem *);

struct heap *heap_new(cmp_elem_t f);
void heap_free(struct heap *h);
struct heap_elem *heap_min(struct heap *h);
void heap_push(struct heap *h, struct heap_elem *e);
struct heap_elem *heap_remove_min(struct heap *h);
void heap_iter(struct heap *h, heap_iter_t);

void heap_elem_init(struct heap_elem *he, vt_t vt, int w, void *e);

#endif
