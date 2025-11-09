#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>

#include "heap.h"

#define N 10

struct elem {
	int id;
	struct heap_elem he;
};
	
void heap_elem_print(struct heap_elem *he) {
	struct elem *e = (struct elem *) he->elem;
	printf("id %d vt %d\n", e->id, he->vruntime);
}

void heap_print(struct heap *heap) {
	printf("h: %d\n", heap->heap_size);
	heap_iter(heap, heap_elem_print);
}

static struct elem* make_elem(int id, int svt) {
	struct elem *e = malloc(sizeof(struct elem));
	e->id = id;
	heap_elem_init(&e->he, svt, 0, e);
	return e;
}

int cmp_elem(struct heap_elem *a, struct heap_elem *b) {
        if (a->vruntime < b->vruntime) return -1;
        if (a->vruntime > b->vruntime) return 1;
	return 0;
}


int main() {
    struct heap *heap = heap_new(cmp_elem);
    struct elem *elems[N];
    int i;

    for (i = 0; i < N; i++) {
	 elems[i] = make_elem(i, i*10);
	 heap_push(heap, &(elems[i]->he));
    }
    
    assert(heap->heap_size == N);

    heap_print(heap);
    
    // peek min
    struct heap_elem *he = heap_min(heap);
    assert(he->elem == elems[0]);
    
    for (i = 0; i < N; i ++) {
	    he = heap_remove_min(heap);
	    assert(he->vruntime == i * 10);
	    struct elem *e = (struct elem *) he->elem;
	    assert(he->vruntime == e->he.vruntime);
	    e->he.vruntime += N*10;
    }

    heap_print(heap);

    for (i = N-1; i >= 0; i--) {
	 heap_push(heap, &(elems[i]->he));
    }

    heap_print(heap);

    for (i = 0; i < N; i ++) {
	    he = heap_remove_min(heap);
	    assert(he->vruntime == (i * 10) + N*10);
	    struct elem *e = (struct elem *) he->elem;
	    assert(he->vruntime == e->he.vruntime);
	    e->he.vruntime += N*10;
    }

    printf("heap tests passed\n");
    return 0;
}


