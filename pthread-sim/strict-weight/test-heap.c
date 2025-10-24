#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>

#include "heap.h"

struct group {
	int spec_virt_time;
	int weight;
	int queued;
	int id;
	struct heap_elem elem;
};
	
void heap_elem_print(struct heap_elem *e) {
	struct group *g = (struct group *) e->elem;
	printf("%d id %d svt %d w %d q %d\n", e->heap_index, g->id, g->spec_virt_time, g->weight, g->queued);
}

void heap_print(struct heap *heap) {
	printf("h: %d\n", heap->heap_size);
	heap_iter(heap, heap_elem_print);
}

static struct group* make_group(int id, int svt, int weight) {
	struct group *g = malloc(sizeof(struct group));
	g->id = id;
	g->spec_virt_time = svt;
	g->queued = 1; // mark as present in heap
	heap_elem_init(&g->elem, g);
	return g;
}

int avg_spec_virt_time(struct heap *heap, struct group *ignore) {
	int total_spec_virt_time = 0;
        int count = 0;
        for (int i = 0; i < heap->heap_size; i++) {
                struct group *g = (struct group *) heap_lookup(heap, i);
		if (g == ignore) continue;
                total_spec_virt_time += g->spec_virt_time;
                count++;
        }
        if (count == 0) return 0;
        return total_spec_virt_time / count;
}

int cmp_elem(void *e0, void *e1) {
        struct group *a = (struct group *) e0;
        struct group *b = (struct group *) e1;
        if (a->queued == 0) return 1;
        if (b->queued == 0) return -1;
        // Compare by spec_virt_time; lower is higher priority
        if (a->spec_virt_time < b->spec_virt_time) return -1;
        if (a->spec_virt_time > b->spec_virt_time) return 1;
        // tie-breaker by group_id for determinism
        if (a->id < b->id) return -1;
        if (a->id > b->id) return 1;
	return 0;
}


int main() {
    // minimal global init
    struct heap *heap = heap_new(cmp_elem);

    struct group *g3 = make_group(3, 30, 10);
    struct group *g1 = make_group(1, 10, 10);
    struct group *g4 = make_group(4, 40, 10);
    struct group *g2 = make_group(2, 20, 10);

    // push in arbitrary order
    heap_push(heap, &g3->elem);
    heap_push(heap, &g1->elem);
    heap_push(heap, &g4->elem);
    heap_push(heap, &g2->elem);


    assert(heap->heap_size == 4);
    
    // avg should ignore none and be (10+20+30+40)/4 = 25
    int avg = avg_spec_virt_time(heap, NULL);
    assert(avg == 25);

    // peek mins in order without removing: expect current min is g1
    struct group *m;
    m = (struct group *) heap_min(heap);
    assert(m == g1);

    // remove all then re-add and test avg with ignore
    heap_remove_at(heap, &g1->elem);
    heap_remove_at(heap, &g2->elem);
    heap_remove_at(heap, &g3->elem);
    heap_remove_at(heap, &g4->elem);

    heap_push(heap, &g1->elem);
    heap_push(heap, &g2->elem);
    heap_push(heap, &g3->elem);
    heap_push(heap, &g4->elem);
    avg = avg_spec_virt_time(heap, g4);
    assert(avg == (10+20+30)/3);

    // Ensure gl_peek_min_group never returns a group with threads_queued == 0
    // Case 1: mark g1 empty and reheapify; expect next min is g2 (non-empty)
    g2->queued = 0;
    heap_fix_index(heap, &g2->elem);

    m = heap_min(heap);
    assert(m != NULL);
    int tq = m->queued;
    assert(tq > 0);
    assert(m == g1); // g1(10) is the min among non-empty (g1,g3,g4)

    // Case 2: mark g3 empty as well; expect min is g1 (still non-empty)
    g3->queued = 0;
    heap_fix_index(heap, &g3->elem);

    m = heap_min(heap);
    assert(m != NULL);
    tq = m->queued;
    assert(tq > 0);
    // Non-empty set is {g1, g4}; min by SVT is g1(10)
    assert(m == g1);

    // Case 3: mark g1 empty too; only g4 remains non-empty => peek must return g4
    g1->queued = 0;
    heap_fix_index(heap, &g1->elem);

    m = heap_min(heap);
    assert(m == g4);
    tq = m->queued;
    assert(tq > 0);

    // Case 4: mark all empty; now peek may return any, but it must not crash
    g4->queued = 0;
    heap_fix_index(heap, &g4->elem);

    m = heap_min(heap);
    assert(m->queued == 0);

    printf("heap tests passed\n");
    return 0;
}


