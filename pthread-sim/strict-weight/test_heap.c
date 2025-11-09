#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>

#include "heap.h"

#define N 10

struct elem {
	int vt;
	int id;
	struct heap_elem elem;
};
	
void heap_elem_print(struct heap_elem *he) {
	struct elem *e = (struct elem *) he->elem;
	printf("%d id %d svt %d w %d q %d\n", he->heap_index, e->id, e->vt);
}

void heap_print(struct heap *heap) {
	printf("h: %d\n", heap->heap_size);
	heap_iter(heap, heap_elem_print);
}

static struct elem* make_elem(int id, int svt) {
	struct elem *e = malloc(sizeof(struct elem));
	e->id = id;
	e->vt = svt;
	heap_elem_init(&e->elem, e);
	return e;
}

int cmp_elem(void *e0, void *e1) {
        struct elem *a = (struct elem *) e0;
        struct elem *b = (struct elem *) e1;
        // Compare by vt; lower is higher priority
        if (a->vt < b->vt) return -1;
        if (a->vt > b->vt) return 1;
        // tie-breaker by group_id for determinism
        if (a->id < b->id) return -1;
        if (a->id > b->id) return 1;
	return 0;
}


int main() {
    struct heap *heap = heap_new(cmp_elem);
    struct elem *elems[N];
    int i;

    for (i = 0; i < N; i++) {
	 elems[i] = make_elem(i, i*10);
	 heap_push(heap, &(elems[i]->elem));
    }
    
    assert(heap->heap_size == N);

    // heap_print(heap);
    
    // peek min
    struct elem *e;
    e = (struct elem *) heap_min(heap);
    assert(e == elems[0]);
    
    for (i = 0; i < N; i ++) {
	    struct elem *e = (struct elem *) heap_remove_min(heap);
	    assert(e->vt == i * 10);
	    e->vt += N*10;
    }

    for (i = N-1; i >= 0; i--) {
	 heap_push(heap, &(elems[i]->elem));
    }

    for (i = 0; i < N; i ++) {
	    struct elem *e = (struct elem *) heap_remove_min(heap);
	    assert(e->vt == (i * 10) + N*10);
	    e->vt += N*10;
    }

    printf("heap tests passed\n");
    return 0;
}


