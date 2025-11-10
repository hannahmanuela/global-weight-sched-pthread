#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "heap.h"

#define HEAP_CAPACITY 64    // XXX todo: reallocating while running mh_min_atomic
#define D_ARY 2
#define CACHE_LINE_SZ 64

static void heap_alloc(struct heap *h) {
	h->heap = aligned_alloc(CACHE_LINE_SZ, sizeof(struct heap_elem) * HEAP_CAPACITY);
	long a = (long) &(h->heap[0]);
	assert(a % CACHE_LINE_SZ == 0);
	printf("a %p sz %d\n", a, sizeof(struct heap_elem));
	h->heap_capacity = HEAP_CAPACITY;
}

struct heap *heap_new(cmp_elem_t cmp) {
	struct heap *h = malloc(sizeof(struct heap));
	h->cmp_elem = cmp;
	h->heap_size = 0;
	h->heap_capacity = 0;
	heap_alloc(h);
	return h;
}

void heap_free(struct heap *h) {
	free(h->heap);
}

void heap_elem_init(struct heap_elem *he, vt_t vt, int w, void *e) {
	he->vruntime = vt;
	he->weight = w;
	he->elem = e;
}

struct heap_elem *heap_min(struct heap *h) {
	if (h->heap_size == 0)
		return NULL;
	struct heap_elem *he = &(h->heap[0]);
	assert( ((long) he) % CACHE_LINE_SZ == 0);
	return he;
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
		if (h->cmp_elem(&(h->heap[idx]), &(h->heap[parent])) < 0) {
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
			if ((c < n) && h->cmp_elem(&(h->heap[c]), &(h->heap[smallest])) < 0) {
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
}

struct heap_elem *heap_remove_min(struct heap *h) {
	if(h->heap_size == 0)
		return NULL;
	int last = h->heap_size - 1;
	h->heap_size--;
	assert(h->heap_size > 0);
	if(last != 0) {
		heap_swap(h, 0, last);
		heap_sift_down(h, 0);
	}
	return &(h->heap[last]);
}
