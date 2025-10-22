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

void mh_check_min_group(struct mheap *mh, struct group *g0) {
	struct group *min;
	int n = 0;
	for (int i = 0; i < mh->nheap; i++) {
		struct lock_heap *lh = mh_heap(mh, i);
		struct group *g1 = (struct group *) heap_min(lh->heap);
		if(g1 && (g0->spec_virt_time > g1->spec_virt_time)) {
			min = g1;
			n++;
		}
	}
	if (min != NULL)
		printf("%d(%d) min %d(%d) n %d\n", g0->spec_virt_time, g0->group_id, min->spec_virt_time, min->group_id, n);
}

struct group *mh_sample_min_group(struct mheap *mh) {
retry:
	int i = random() % mh->nheap;
	int j = random() % mh->nheap;
	while (i == j) {
		j = random() % mh->nheap;
	}
	struct lock_heap *lh_i = mh_heap(mh, i);
	struct lock_heap *lh_j = mh_heap(mh, j);
	// XXX use atomics
	struct group *g_i = (struct group *) heap_min(lh_i->heap);
	struct group *g_j = (struct group *) heap_min(lh_j->heap);
	if (g_i == NULL && g_j == NULL) {
		return NULL;
	}
	if (g_i == NULL) {
		g_i = g_j;
		lh_i = lh_j;
	} else if (g_j && lh_i->heap->cmp_elem(g_i, g_j) > 0) {
		g_i = g_j;
		lh_i = lh_j;
	}
	if(lh_try_lock(lh_i) != 0)
		goto retry;
	return g_i;
}

// returns with heap locked
struct group *mh_min_group(struct mheap *mh) {
	if (mh->nheap == 1) {
		struct lock_heap *lh = mh_heap(mh, 0);
		lh_lock_timed(lh);
		struct group *g = (struct group *) heap_min(lh->heap);
		if(g == NULL) {
			lh_unlock(lh);
			return NULL;
		}	
		return g;
	}
	return mh_sample_min_group(mh);
}

