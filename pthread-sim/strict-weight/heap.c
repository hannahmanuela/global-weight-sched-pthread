#include <assert.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>

#include "util.h"
#include "lock.h"
#include "heap.h"

#define D_ARY 4

static void heap_alloc(struct heap *h) {
	//h->heap = aligned_alloc(CACHE_LINE_SZ, sizeof(struct heap_elem) * HEAP_CAPACITY);
	assert(sizeof(struct heap_elem) == 16);
	h->heap_capacity = HEAP_CAPACITY;
}

struct heap *heap_new() {
	struct heap *h = aligned_alloc(CACHE_LINE_SZ, (sizeof(struct heap)));
	h->heap_size = 0;
	h->heap_capacity = 0;
	lock_init(&h->lk);
	heap_alloc(h);
	assert(((long) h->heap) % CACHE_LINE_SZ == 0);
	assert(((long) (&h->heap)) % CACHE_LINE_SZ == 0);
	assert((((long) (&h->lk)) % CACHE_LINE_SZ) == 0);
	assert((((long) (&h->heap_size)) % CACHE_LINE_SZ) == 0);
	return h;
}

void heap_free(struct heap *h) {
	// free(h->heap);
}

void heap_elem_init(struct heap_elem *he, vt_t vt, int w, void *e) {
	he->vruntime = vt;
	he->weight = w;
	he->elem = e;
}

static int heap_elem_cmp(struct heap_elem *a, struct heap_elem *b) {
	// Compare by vruntime; lower is higher priority
	if (a->vruntime < b->vruntime) return -1;
	if (a->vruntime > b->vruntime) return 1;
	// Prefer higher weight
	if (a->weight > b->weight) return -1;
	if (a->weight < b->weight) return 1;
	return 0;
}

struct heap_elem *heap_min(struct heap *h) {
	if (h->heap_size == 0)
		return NULL;
	return h->heap;
} 

void heap_iter(struct heap *heap, heap_iter_t iter) {
	for (int i = 0; i < heap->heap_size; i++) {
		iter(&heap->heap[i]);
	}
}

static inline void heap_swap(struct heap *h, int i, int j) {
	struct heap_elem tmp = h->heap[i];
	h->heap[i] = h->heap[j];
	h->heap[j] = tmp;
}

static void heap_sift_up(struct heap *h, int idx) {
	while (idx > 0) {
		int parent = (idx - 1) / D_ARY;
		if (heap_elem_cmp(&(h->heap[idx]), &(h->heap[parent])) < 0) {
			heap_swap(h, idx, parent);
			idx = parent;
		} else {
			break;
		}
	}
}

static void heap_sift_down(struct heap *h, int idx) {
	int n = h->heap_size;
	while (1) {
		int left = idx * D_ARY + 1;
		int smallest = idx;
		for (int i = 0; i < D_ARY; i++) {
			int c = left + i;
			if ((c < n) && heap_elem_cmp(&(h->heap[c]), &(h->heap[smallest])) < 0) {
				smallest = c;
			}
		}
		if (smallest != idx) {
			heap_swap(h, idx, smallest);
			idx = smallest;
		} else {
			break;
		}
	}
}


void heap_push(struct heap *h, struct heap_elem *e) {
	assert(h->heap_size+1 < h->heap_capacity);
	int i = h->heap_size;
	h->heap[h->heap_size++] = *e;
	heap_sift_up(h, i);
	//h->min_vt = h->heap[0].vruntime;
}

struct heap_elem *heap_remove_min(struct heap *h) {
	if(h->heap_size == 0)
		return NULL;
	int last = h->heap_size - 1;
	h->heap_size--;
	if(last != 0) {
		heap_swap(h, 0, last);
		heap_sift_down(h, 0);
		// h->min_vt = h->heap[0].vruntime;
	}
	return &(h->heap[last]);
}
