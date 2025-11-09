#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "heap.h"

#define MIN_CAPACITY 64    // XXX todo: reallocating while running mh_min_atomic

static void heap_ensure_capacity(struct heap *h) {
	if (h->heap_size < h->heap_capacity) return;
	int new_capacity = h->heap_capacity == 0 ? MIN_CAPACITY : h->heap_capacity * 2;
	h->heap = realloc(h->heap, sizeof(struct heap_elem*) * new_capacity);
	h->heap_capacity = new_capacity;
}

struct heap *heap_new(int cmp(void *, void *)) {
	struct heap *h = malloc(sizeof(struct heap));
	h->cmp_elem = cmp;
	h->heap_size = 0;
	h->heap_capacity = 0;
	h->heap = NULL;
	heap_ensure_capacity(h);
	return h;
}

void heap_free(struct heap *h) {
	free(h->heap);
}

void heap_elem_init(struct heap_elem *h, void *e) {
	h->elem = e;
}

void *heap_min(struct heap *h) {
	if (h->heap_size == 0)
		return NULL;
	return h->heap[0]->elem;
} 

void heap_iter(struct heap *heap, void (*iter)(struct heap_elem *)) {
	for (int i = 0; i < heap->heap_size; i++) {
		iter(heap->heap[i]);
	}
}

static inline void heap_swap(struct heap *h, int i, int j) {
	void *tmp = h->heap[i];
	h->heap[i] = h->heap[j];
	h->heap[j] = tmp;
}

static void heap_sift_up(struct heap *h, int idx) {
	while (idx > 0) {
		int parent = (idx - 1) / 2;
		if (h->cmp_elem(h->heap[idx]->elem, h->heap[parent]->elem) < 0) {
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
		int left = idx * 2 + 1;
		int right = idx * 2 + 2;
		int smallest = idx;
		if ((left < n) && h->cmp_elem(h->heap[left]->elem, h->heap[smallest]->elem) < 0) {
			smallest = left;
		}
		if ((right < n) && h->cmp_elem(h->heap[right]->elem, h->heap[smallest]->elem) < 0) {
			smallest = right;
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
	heap_ensure_capacity(h);
	int i = h->heap_size;
	h->heap[h->heap_size++] = e;
	heap_sift_up(h, i);
}

void *heap_remove_min(struct heap *h) {
	if(h->heap_size == 0)
		return NULL;
	int last = h->heap_size - 1;
	struct heap_elem *he = h->heap[0];
	h->heap_size--;
	if(last == 0) {
		return he->elem;
	}
	h->heap[0] = h->heap[last];
	heap_sift_down(h, 0);
	return he->elem;
}
