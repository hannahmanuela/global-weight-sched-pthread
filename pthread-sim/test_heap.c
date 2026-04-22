#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>

#include "heap.h"
#include "util.h"

#define N 4

bool do_affinity = false;

struct elem {
	int id;
	struct heap_elem he;
};
	
void heap_elem_print(struct heap_elem *he) {
	struct elem *e = container_of(he, struct elem, he);
	printf("id %d vt %lld\n", e->id, he->vruntime);
}

void heap_print(struct heap *heap) {
	printf("h: %d\n", heap->heap_size);
	heap_iter(heap, heap_elem_print);
}

static struct elem* make_elem(int id, int vt) {
	struct elem *e = malloc(sizeof(struct elem));
	e->id = id;
	heap_elem_init(&e->he, vt, 0, e);
	return e;
}

int cmp_elem(struct heap_elem *a, struct heap_elem *b) {
        if (a->vruntime < b->vruntime) return -1;
        if (a->vruntime > b->vruntime) return 1;
	return 0;
}

int main() {
	struct heap *heap = heap_new();
	struct elem *elems[N];
	int i;

	for (i = 0; i < N; i++) {
		elems[i] = make_elem(i, i*N);
		heap_push(heap, &(elems[i]->he));
	}
    
	assert(heap->heap_size == N);

	// peek min
	struct heap_elem *he = heap_min(heap);
    
	for (i = 0; i < N; i ++) {
		he = heap_remove_min(heap);
		assert(he->vruntime == i * N);
		struct elem *e = (struct elem *) he->elem;
		assert(he->vruntime == e->he.vruntime);
		e->he.vruntime += N*N;
	}

	heap_print(heap);

	for (i = N-1; i >= 0; i--) {
		heap_push(heap, &(elems[i]->he));
	}

	heap_print(heap);

	for (i = 0; i < N; i ++) {
		he = heap_remove_min(heap);
		assert(he->vruntime == (i * N) + N*N);
		struct elem *e = (struct elem *) he->elem;
		assert(he->vruntime == e->he.vruntime);		
		e->he.vruntime += N*N;
	}

	printf("heap tests passed\n");
	return 0;
}


