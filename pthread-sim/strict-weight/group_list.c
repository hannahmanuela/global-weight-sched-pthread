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
	printf("\nLock timing statistics:\n");
	mh_stats(glist->mheap);
}

// returns with group and heap locked
struct group* gl_get_min_group(struct group_list *gl) {
	struct lock_heap *lh = mh_heap(gl->mheap, 0);
	lh_lock_timed(lh);
	struct group *g = (struct group *) heap_min(lh->heap);
	g->lh = lh;
	if (g && g->threads_queued == 0) {
		g = NULL;
	}
	if (g) {
		pthread_rwlock_wrlock(&g->group_lock);
	}
	return g;
}

// compute avg_spec_virt_time for groups in gl, ignoring group_to_ignore
// caller should have no locks
int gl_avg_spec_virt_time(struct group_list *gl, struct group *group_to_ignore) {
	int total_spec_virt_time = 0;
	int count = 0;
	struct lock_heap *lh = mh_heap(gl->mheap, 0);

	lh_rdlock_timed(lh);
	for (struct heap_elem *e = heap_first(lh->heap); e != NULL; e = heap_next(lh->heap, e)) {
		struct group *g = (struct group *) e->elem;
		assert(g != NULL);
		if (g == group_to_ignore) continue;
		total_spec_virt_time += grp_get_spec_virt_time(g);
		count++;
	}
	lh_unlock(lh);
	if (count == 0) return 0;
	return total_spec_virt_time / count;
}


// add group to heap; caller must hold heap_lock
void gl_add_group(struct group_list *gl, struct group *g) {
	struct lock_heap *lh = mh_heap(gl->mheap, 0);
	heap_push(lh->heap, &g->heap_elem);
}

// delete group from heap; caller must hold heap_lock
void gl_del_group(struct group_list *gl, struct group *g) {
	struct lock_heap *lh = mh_heap(gl->mheap, 0);
	heap_remove_at(lh->heap, &g->heap_elem);
}

// Update group's spec_virt_time and safely reheapify if the group is in the heap.
// caller must hold heap lock and group_lock
void gl_update_group_svt(struct group *g, int diff) {
	g->spec_virt_time += diff;
	heap_fix_index(g->lh->heap, &g->heap_elem);
}

void gl_fix_group(struct group *g) {
	lh_lock_timed(g->lh);
        pthread_rwlock_wrlock(&g->group_lock);
        // If the group is currently in the heap, fix its position
        if (g->heap_elem.heap_index != -1) {
		heap_fix_index(g->lh->heap, &g->heap_elem);
        }
        pthread_rwlock_unlock(&g->group_lock);
        lh_unlock(g->lh);
}

void gl_register_group(struct group_list *gl, struct group *g) {
	struct lock_heap *lh = mh_heap(gl->mheap, 0);
	lh_lock_timed(lh);
	gl_add_group(gl, g);
	lh_unlock(lh);
}

void gl_unregister_group(struct group_list *gl, struct group *g) {
	struct lock_heap *lh = mh_heap(gl->mheap, 0);
	lh_lock_timed(lh);
	gl_del_group(gl, g);
	lh_unlock(lh);
}
