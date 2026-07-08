#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>

#include "heap.h"
#include "util.h"

#define N 8

bool do_affinity = false;
bool do_latency = false;

struct elem {
	int id;
	struct heap_elem he;
};
	
void heap_elem_print(struct heap_elem *he) {
	struct elem *e = container_of(he, struct elem, he);
	printf("id %d vt %lld %p\n", e->id, he->vruntime, he);
}

void heap_print(struct heap *heap) {
	printf("h: %d\n", heap->heap_size);
	heap_iter(heap, heap_elem_print);
}

static struct elem* make_elem(int id, int vt) {
	struct elem *e = malloc(sizeof(struct elem));
	e->id = id;
	heap_elem_init(&e->he, vt, 0);
	return e;
}

void test_min() {
	struct heap *heap = heap_new(is_lt_elem_vt_w);
	struct elem *elems[N];
	int i;

	for (i = 0; i < N; i++) {
		elems[i] = make_elem(i, i*N);
		heap_push(heap, &elems[i]->he);
	}
    
	assert(heap->heap_size == N);

	// peek min
	struct heap_elem *he = heap_min(heap);
    
	for (i = 0; i < N; i ++) {
		he = heap_remove_min(heap);
		assert(he->vruntime == i * N);
		struct elem *e = container_of(he, struct elem, he);
		assert(he->vruntime == e->he.vruntime);
		e->he.vruntime += N*N;
	}

	heap_print(heap);

	for (i = N-1; i >= 0; i--) {
		heap_push(heap, &(elems[i]->he));
	}

	//heap_print(heap);

	for (i = 0; i < N; i ++) {
		he = heap_remove_min(heap);
		assert(he->vruntime == (i * N) + N*N);
		struct elem *e = container_of(he, struct elem, he);
		assert(he->vruntime == e->he.vruntime);		
		e->he.vruntime += N*N;
	}
	printf("heap tests min passed\n");

}

void test_erase() {
	struct heap *heap = heap_new(is_lt_elem_vt_w);
	struct elem *elems[N];
	int i;

	for (i = 0; i < N; i++) {
		elems[i] = make_elem(i, i*N);
		heap_push(heap, &(elems[i]->he));
	}
    
	assert(heap->heap_size == N);
	// int o = N/2;
	int o = 1;
	for (i = o; i < N + o; i++) {
		printf("erase :");
		heap_elem_print(&(elems[i%N]->he));
		heap_print(heap);
		bool ok = heap_erase(heap, &(elems[i % N])->he);
		assert(ok);
	}
}

int main() {
	test_min();
	test_erase();
	return 0;
}


