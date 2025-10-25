#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "heap.h"

struct heap *heap_new(int cmp(void *, void *)) {
	struct heap *h = malloc(sizeof(struct heap));
	h->cmp_elem = cmp;
	h->heap_size = 0;
	h->heap_capacity = 0;
	return h;
}

void heap_elem_init(struct heap_elem *h, void *e) {
	h->heap_index = -1;
	h->elem = e;
}

int heap_elem_idx(struct heap_elem *h) {
	return h->heap_index;
}

void *heap_min(struct heap *h) {
	if (h->heap_size == 0)
		return NULL;
	return h->heap[0]->elem;
} 

void *heap_lookup(struct heap *heap, int idx) {
	return heap->heap_size > 0 ? heap->heap[idx]->elem : NULL;
}

void heap_iter(struct heap *heap, void (*iter)(struct heap_elem *)) {
	for (int i = 0; i < heap->heap_size; i++) {
		iter(heap->heap[i]);
	}
}

struct heap_elem* heap_first(struct heap *h) {
	if(h->heap_size <= 0)
		return NULL;
	return h->heap[0];
}

struct heap_elem* heap_next(struct heap *h, struct heap_elem *e) {
	int i = e->heap_index + 1;
	if(i >= h->heap_size)
		return NULL;
	return h->heap[i];
}

static inline void heap_swap(struct heap *h, int i, int j) {
	void *tmp = h->heap[i];
	h->heap[i] = h->heap[j];
	h->heap[j] = tmp;
	h->heap[i]->heap_index = i;
	h->heap[j]->heap_index = j;
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

void heap_ensure_capacity(struct heap *h) {
	if (h->heap_size < h->heap_capacity) return;
	int new_capacity = h->heap_capacity == 0 ? 16 : h->heap_capacity * 2;
	h->heap = realloc(h->heap, sizeof(struct heap_elem*) * new_capacity);
	h->heap_capacity = new_capacity;
}

// reheapify an elem at its current index after its key changed
void heap_fix_index(struct heap *h, struct heap_elem *e) {
    assert(e->heap_index != -1);
	if (e->heap_index < 0 || e->heap_index >= h->heap_size) return;
	heap_sift_down(h, e->heap_index);
	heap_sift_up(h, e->heap_index);
}

// push an elem into heap
void heap_push(struct heap *h, struct heap_elem *e) {
    assert(e->heap_index == -1);
    heap_ensure_capacity(h);
    e->heap_index = h->heap_size;
    h->heap[h->heap_size++] = e;
    heap_sift_up(h, e->heap_index);
}

// remove elem at index
void heap_remove_at(struct heap *h, struct heap_elem *e) {
    assert(e->heap_index != -1);
    int last = h->heap_size - 1;
    if (e->heap_index < 0 || e->heap_index >= h->heap_size) return;
    if (e->heap_index != last) {
        heap_swap(h, e->heap_index, last);
    }
    struct heap_elem *removed = h->heap[last];
    h->heap_size--;
    if (e->heap_index < h->heap_size) {
        heap_sift_down(h, e->heap_index);
        heap_sift_up(h, e->heap_index);
    }
    removed->heap_index = -1;
}
