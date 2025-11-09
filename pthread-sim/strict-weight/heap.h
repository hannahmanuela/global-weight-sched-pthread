#ifndef _HEAP_H_

#define _HEAP_H_

#include "heap_elem.h"

struct heap {
	struct heap_elem *heap;
	cmp_elem_t cmp_elem;
	int heap_capacity;
	int heap_size;
}  __attribute__((aligned(64)));

typedef void (*heap_iter_t)(struct heap_elem *);

struct heap *heap_new(cmp_elem_t f);
void heap_free(struct heap *h);
struct heap_elem *heap_min(struct heap *h);
void heap_push(struct heap *h, struct heap_elem *e);
struct heap_elem *heap_remove_min(struct heap *h);
void heap_iter(struct heap *h, heap_iter_t);

void heap_elem_init(struct heap_elem *he, vt_t vt, int w, void *e);

#endif
	
