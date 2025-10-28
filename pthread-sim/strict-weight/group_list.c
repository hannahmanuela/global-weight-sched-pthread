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
	glist->mh = mh_new(grp_cmp, nqueue);
	return glist;
}

static void print_elem(struct heap_elem *e) {
	struct group *g = (struct group *) e->elem;
	printf("(gid %d svt %d, n %d, q %d)", g->group_id, g->spec_virt_time, g->num_threads, g->threads_queued);
}

void gl_print(struct group_list *gl) {
	mh_print(gl->mh, print_elem);
}

void gl_lock_stats(struct group_list *glist) {
	mh_lock_stats(glist->mh);
}

void gl_runtime_stats(struct group_list *glist) {
	mh_runtime_stats(glist->mh);
}

// returns with group and heap locked
struct group* gl_min_group(struct group_list *gl) {
	struct group *g = mh_min_group(gl->mh);
	if (g && g->threads_queued == 0) {
		g = NULL;
	}
	if (g) {
		// mh_check_min_group(gl->mh, g);
		pthread_rwlock_wrlock(&g->group_lock);
	}
	return g;
}

// XXX move
int gl_avg_spec_virt_time_inc(struct lock_heap *lh) {
	if (lh->heap->n <= 0)
		return 0;
	int sum = lh->heap->sum / lh->heap->n;
	return sum;
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
	int sum = lh->heap->sum - group_to_ignore->spec_virt_time;
	printf("total %d sum %d cnt %d hs %d\n", total_spec_virt_time, sum, count, lh->heap->heap_size-2);
	if (count == 0) return 0;
	return total_spec_virt_time / count;
}

void gl_register_group(struct group_list *gl, struct group *g) {
	mh_add_group(gl->mh, g);
}

void gl_unregister_group(struct group_list *gl, struct group *g) {
	mh_del_group(gl->mh, g);
}
