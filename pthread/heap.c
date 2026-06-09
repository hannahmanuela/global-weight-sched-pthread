#include <assert.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>

#include "util.h"
#include "lock.h"
#include "heap.h"

#define D_ARY 4
//#define D_ARY 2

static void heap_alloc(struct heap *h) {
	// h->heap = aligned_alloc(CACHE_LINE_SZ, sizeof(struct heap_elem) * HEAP_CAPACITY);
	assert(sizeof(struct heap_elem) == 16);
	h->heap_capacity = HEAP_CAPACITY;
}

struct heap *heap_new() {
	struct heap *h = aligned_alloc(CACHE_LINE_SZ, ALIGN_UP(sizeof(struct heap), CACHE_LINE_SZ));
	h->heap_size = 0;
	h->heap_capacity = 0;
	h->last_vt = 0;
	h->max = 0;
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

void heap_elem_init(struct heap_elem *he, vt_t vt, int w) {
	he->vruntime = vt;
	he->weight = w;
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
	return h->heap[0];
} 

void heap_iter(struct heap *heap, heap_iter_t iter) {
	for (int i = 0; i < heap->heap_size; i++) {
		iter(heap->heap[i]);
	}
}

static inline void heap_swap(struct heap *h, int i, int j) {
	struct heap_elem *tmp = h->heap[i];
	h->heap[i] = h->heap[j];
	h->heap[j] = tmp;
	h->heap[i]->idx = i;
	h->heap[j]->idx = j;
}

static void heap_sift_up(struct heap *h, int idx) {
	while (idx > 0) {
		int parent = (idx - 1) / D_ARY;
		if (heap_elem_cmp(h->heap[idx], h->heap[parent]) < 0) {
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
			if ((c < n) && heap_elem_cmp(h->heap[c], h->heap[smallest]) < 0) {
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
	e->idx = h->heap_size;
	h->heap[e->idx] = e;
	h->heap_size++;
	heap_sift_up(h, e->idx);
	if (e->idx > h->max)
		h->max = e->idx;
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
	struct heap_elem *he = h->heap[last];
	h->last_vt = he->vruntime;
	he->idx = -1;
	return he;
}


bool heap_erase(struct heap *h, struct heap_elem *e) {
	int i = e->idx;
        if (i == -1) 
                return false;

	int last = h->heap_size - 1;
	h->heap_size--;
        if (last != i) {
		heap_swap(h, i, last);
                heap_sift_up(h, i);
                heap_sift_down(h, i);
        }
	e->idx = -1;
	return true;
}
