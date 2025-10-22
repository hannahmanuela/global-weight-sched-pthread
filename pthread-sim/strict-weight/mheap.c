#include <stdlib.h>
#include <stdio.h>

#include "group.h"
#include "lheap.h"
#include "mheap.h"

struct mheap *mh_new(int grp_cmp(void *, void *), int n) {
	struct mheap *mh = malloc(sizeof(struct mheap));
	mh->lh = (struct lock_heap **) malloc(sizeof(struct lock_heap) * n);
	for (int i=0; i < n; i++) {
		mh->lh[i] = lh_new(grp_cmp);
	}
	mh->nheap = n;
	return mh;
}

void mh_print(struct mheap *mh, void print_elem(struct heap_elem*)) {
	for (int i = 0; i < mh->nheap; i++) {
		lh_rdlock_timed(mh->lh[i]);
		printf("Global heap size: %d \n", mh->lh[i]->heap->heap_size);
		printf("Heap contents by (group_id: svt, num_threads, num_queued): ");
		heap_iter(mh->lh[i]->heap, print_elem);
		printf("\n");
		lh_unlock(mh->lh[i]);
	}
}

void mh_stats(struct mheap *mh) {
	for (int i = 0; i < mh->nheap; i++) {
		printf("== heap %d:\n", i);
		lh_stats(mh->lh[i]);
	}
}

struct lock_heap *mh_heap(struct mheap *mh, int i) {
	return mh->lh[i];
}

void mh_add_group(struct mheap *mh, struct group *g) {
	int i = random() % mh->nheap;
	struct lock_heap *lh = mh_heap(mh, i);
	g->lh = lh;
	lh_lock_timed(lh);
	heap_push(lh->heap, &g->heap_elem);
	lh_unlock(lh);
}

void mh_del_group(struct mheap *mh, struct group *g) {
	lh_lock_timed(g->lh);
	heap_remove_at(g->lh->heap, &g->heap_elem);
	lh_unlock(g->lh);
}

// returns with heap locked
struct group *mh_min_group(struct mheap *mh) {
	int i = random() % mh->nheap;
	int j = random() % mh->nheap;
	if(i == j) {
		struct lock_heap *lh = mh_heap(mh, i);
		lh_lock_timed(lh);   // XXX is read lock sufficient?
		struct group *g = (struct group *) heap_min(lh->heap);
		if(g == NULL) {
			lh_unlock(lh);
			return NULL;
		}	
		g->lh = lh;   // XXX unnecessary; do this at register/unregister	
		return g;
	}
	if (i > j) {
		int t = i;
		i = j;
		j = t;
	}
	struct lock_heap *lh_i = mh_heap(mh, i);
	struct lock_heap *lh_j = mh_heap(mh, j);
	lh_lock_timed(lh_i);   // XXX is read lock sufficient?
	lh_lock_timed(lh_j);   // XXX is read lock sufficient?
	struct group *g_i = (struct group *) heap_min(lh_i->heap);
	struct group *g_j = (struct group *) heap_min(lh_j->heap);
	if (g_i == NULL && g_j == NULL) {
		lh_unlock(lh_i);
		lh_unlock(lh_j);
		return NULL;
	}
	if (g_i == NULL) {
		lh_unlock(lh_i);
		return g_j;
	}
	if (g_j == NULL) {
		lh_unlock(lh_j);
		return g_i;
	}
	if (lh_i->heap->cmp_elem(g_i, g_j) < 0) {
		lh_unlock(lh_j);
		return g_i;
	}
	lh_unlock(lh_i);
	return g_j;
}

