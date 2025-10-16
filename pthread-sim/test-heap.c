#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>

#define UNIT_TEST

#include "strict-weight/global-accounting-rlock.c"

// Peek min using gl_peek_min_group and immediately unlock held locks
static struct group* peek_min_and_unlock(struct group_list *gl) {
    struct group *g = gl_peek_min_group(gl); // returns with list and group locks held
    if (g) {
        pthread_rwlock_unlock(&g->group_lock);
    }
    pthread_rwlock_unlock(&gl->group_list_lock);
    return g;
}

static struct group* make_group(int id, int svt, int weight) {
    struct group *g = create_group(id, weight);
    g->spec_virt_time = svt;
    g->threads_queued = 1; // mark as present in heap
    return g;
}

int main() {
    // minimal global init
    gs = malloc(sizeof(struct global_state));
    gs->glist = (struct group_list*) malloc(sizeof(struct group_list));
    gs->glist->heap = NULL;
    gs->glist->heap_size = 0;
    gs->glist->heap_capacity = 0;
    pthread_rwlock_init(&gs->glist->group_list_lock, NULL);
    num_groups = 5;

    struct group *g3 = make_group(3, 30, 10);
    struct group *g1 = make_group(1, 10, 10);
    struct group *g4 = make_group(4, 40, 10);
    struct group *g2 = make_group(2, 20, 10);

    // push in arbitrary order
    gl_add_group(gs->glist, g3);
    gl_add_group(gs->glist, g1);
    gl_add_group(gs->glist, g4);
    gl_add_group(gs->glist, g2);

    assert(gs->glist->heap_size == 4);
    
    // avg should ignore none and be (10+20+30+40)/4 = 25
    int avg = gl_avg_spec_virt_time(gs->glist, NULL);
    assert(avg == 25);

    // peek mins in order without removing: expect current min is g1
    struct group *m;
    m = peek_min_and_unlock(gs->glist); assert(m == g1);

    // remove all then re-add and test avg with ignore (single-threaded: no explicit list lock)
    gl_del_group(gs->glist, g1);
    gl_del_group(gs->glist, g2);
    gl_del_group(gs->glist, g3);
    gl_del_group(gs->glist, g4);

    gl_add_group(gs->glist, g1);
    gl_add_group(gs->glist, g2);
    gl_add_group(gs->glist, g3);
    gl_add_group(gs->glist, g4);
    avg = gl_avg_spec_virt_time(gs->glist, g4);
    assert(avg == (10+20+30)/3);

    // Ensure gl_peek_min_group never returns a group with threads_queued == 0
    // Case 1: mark g1 empty and reheapify; expect next min is g2 (non-empty)
    pthread_rwlock_wrlock(&g2->group_lock);
    g2->threads_queued = 0;
    pthread_rwlock_unlock(&g2->group_lock);

    gl_heap_fix_index(gs->glist, g2->heap_index);

    m = peek_min_and_unlock(gs->glist); assert(m != NULL);
    pthread_rwlock_rdlock(&m->group_lock);
    int tq = m->threads_queued;
    pthread_rwlock_unlock(&m->group_lock);
    assert(tq > 0);
    assert(m == g1); // g1(10) is the min among non-empty (g1,g3,g4)

    // Case 2: mark g3 empty as well; expect min is g1 (still non-empty)
    pthread_rwlock_wrlock(&g3->group_lock);
    g3->threads_queued = 0;
    pthread_rwlock_unlock(&g3->group_lock);
    gl_heap_fix_index(gs->glist, g3->heap_index);

    m = peek_min_and_unlock(gs->glist); assert(m != NULL);
    pthread_rwlock_rdlock(&m->group_lock); tq = m->threads_queued; pthread_rwlock_unlock(&m->group_lock);
    assert(tq > 0);
    // Non-empty set is {g1, g4}; min by SVT is g1(10)
    assert(m == g1);

    // Case 3: mark g1 empty too; only g4 remains non-empty => peek must return g4
    pthread_rwlock_wrlock(&g1->group_lock);
    g1->threads_queued = 0;
    pthread_rwlock_unlock(&g1->group_lock);
    gl_heap_fix_index(gs->glist, g1->heap_index);

    m = peek_min_and_unlock(gs->glist); assert(m == g4);
    pthread_rwlock_rdlock(&m->group_lock); tq = m->threads_queued; pthread_rwlock_unlock(&m->group_lock);
    assert(tq > 0);

    // Case 4: mark all empty; now peek may return any, but it must not crash
    pthread_rwlock_wrlock(&g4->group_lock);
    g4->threads_queued = 0;
    pthread_rwlock_unlock(&g4->group_lock);
    gl_heap_fix_index(gs->glist, g4->heap_index);

    m = peek_min_and_unlock(gs->glist);
    assert(m == NULL);

    printf("heap tests passed\n");
    return 0;
}


