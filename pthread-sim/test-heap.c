#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>

#define UNIT_TEST

#include "strict-weight/global-accounting-rlock.c"

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
    gs->glist->nfail_min = 0;
    gs->glist->nfail_time = 0;
    gs->glist->nfail_list = 0;
    num_groups = 5;
    num_groups_empty = num_groups;

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
    assert(num_groups_empty == num_groups - 4);

    // avg should ignore none and be (10+20+30+40)/4 = 25
    int avg = gl_avg_spec_virt_time(gs->glist, NULL);
    assert(avg == 25);

    // pop mins in order: g1(10), g2(20), g3(30), g4(40)
    struct group *m;
    m = gl_find_min_group(gs->glist); assert(m == g1);
    m = gl_find_min_group(gs->glist); assert(m == g2);

    // remove an interior item: remove g3 via gl_del_group
    gl_del_group(gs->glist, g3);
    assert(gs->glist->heap_size == 1);

    // remaining min should be g4
    m = gl_find_min_group(gs->glist); assert(m == g4);
    assert(gs->glist->heap_size == 0);

    // re-add and test avg with ignore
    gl_add_group(gs->glist, g1);
    gl_add_group(gs->glist, g2);
    gl_add_group(gs->glist, g3);
    gl_add_group(gs->glist, g4);
    avg = gl_avg_spec_virt_time(gs->glist, g4);
    assert(avg == (10+20+30)/3);

    printf("heap tests passed\n");
    return 0;
}


