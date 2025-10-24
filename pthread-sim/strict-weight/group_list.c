#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "lheap.h"
#include "mheap.h"
#include "group.h"
#include "group_list.h"
#include "util.h"

// lock invariant (to avoid deadlocks): 
//  - always lock global list before group lock, if you're going to lock both 
//      (no locking a group while holding the list lock)

struct group_list *gl_new(int nqueue) {
	struct group_list *glist = (struct group_list *) malloc(sizeof(struct group_list));
	glist = (struct group_list *) malloc(sizeof(struct group_list));
	glist->mheap = mh_new(grp_cmp, nqueue);
	return glist;
}

static void print_elem(struct heap_elem *e) {
	struct group *g = (struct group *) e->elem;
	pthread_rwlock_rdlock(&g->group_lock);
	printf("(%d: %d, %d, %d)", g->group_id, g->spec_virt_time, g->num_threads, g->threads_queued);
	pthread_rwlock_unlock(&g->group_lock);
}

void gl_print(struct group_list *gl) {
	mh_print(gl->mheap, print_elem);
}

void gl_stats(struct group_list *glist) {
	mh_stats(glist->mheap);
}

// returns with group and heap locked
struct group* gl_min_group(struct group_list *gl) {
	struct group *g = mh_min_group(gl->mheap);
	if (g && g->threads_queued == 0) {
		g = NULL;
	}
	if (g) {
		// mh_check_min_group(gl->mheap, g);
		pthread_rwlock_wrlock(&g->group_lock);
	}
	return g;
}

// compute avg_spec_virt_time for groups in group_to_ignore's heap, ignoring group_to_ignore
// caller should have heap lock
int gl_avg_spec_virt_time(struct group *group_to_ignore) {
	int total_spec_virt_time = 0;
	int count = 0;
	struct lock_heap *lh = group_to_ignore->lh;

	for (struct heap_elem *e = heap_first(lh->heap); e != NULL; e = heap_next(lh->heap, e)) {
		struct group *g = (struct group *) e->elem;
		assert(g != NULL);
		if (g == group_to_ignore) continue;
		if (mh_empty(g)) continue;
		total_spec_virt_time += grp_get_spec_virt_time(g);
		count++;
	}
	if (count == 0) return 0;
	return total_spec_virt_time / count;
}


void gl_add_group(struct group_list *gl, struct group *g) {
	mh_add_group(gl->mheap, g);
}

void gl_del_group(struct group_list *gl, struct group *g) {
	mh_del_group(gl->mheap, g);
}

// Update group's spec_virt_time and safely reheapify if the group is in the heap.
// caller must hold heap lock and group_lock
void gl_update_group_svt(struct group *g, int diff) {
	g->spec_virt_time += diff;
	heap_fix_index(g->lh->heap, &g->heap_elem);
}

// caller must hold heap lock and group lock
void gl_fix_group(struct group *g) {
        // If the group is currently in the heap, fix its position
        if (g->heap_elem.heap_index != -1) {
		heap_fix_index(g->lh->heap, &g->heap_elem);
        }
}

void gl_register_group(struct group_list *gl, struct group *g) {
	gl_add_group(gl, g);
}

void gl_unregister_group(struct group_list *gl, struct group *g) {
	gl_del_group(gl, g);
}
