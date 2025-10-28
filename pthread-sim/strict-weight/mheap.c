#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
#include <limits.h>

#include "driver.h"
#include "group.h"
#include "lheap.h"
#include "mheap.h"

#define DUMMY  -1

struct mheap *mh_new(int grp_cmp(void *, void *), int n) {
	struct mheap *mh = malloc(sizeof(struct mheap));
	mh->lh = (struct lock_heap **) malloc(sizeof(struct lock_heap) * n);
	for (int i=0; i < n; i++) {
		mh->lh[i] = lh_new(grp_cmp);
		// insert a dummy element so that the heap always has a min
		struct group* dummy = grp_new(DUMMY, 0);
		dummy->threads_queued = 1;
		dummy->spec_virt_time = INT_MAX;
		heap_push(mh->lh[i]->heap, &dummy->heap_elem);
	}
	mh->nheap = n;
	return mh;
}

int mh_time(struct mheap *mh, int tick_length, int w) {
	return (tick_length/mh->total_weight) * w;
}

int mh_time_inc(struct mheap *mh, int tick_length, int w) {
	return tick_length - mh_time(mh, tick_length, w);
}

int mh_empty(struct group *g) {
	return g->group_id == DUMMY;
}

void mh_print(struct mheap *mh, void print_elem(struct heap_elem*)) {
	printf("mh total weight %d\n", mh->total_weight);
	for (int i = 0; i < mh->nheap; i++) {
		lh_rdlock_timed(mh->lh[i]);
		struct heap *h = mh->lh[i]->heap;
		printf("Heap %d size %d sum %d n %d: \n", i, h->heap_size,
		       h->sum, h->n);
		heap_iter(mh->lh[i]->heap, print_elem);
		printf("\n");
		lh_unlock(mh->lh[i]);
	}
}

void mh_lock_stats(struct mheap *mh) {
	for (int i = 0; i < mh->nheap; i++) {
		printf("== heap %d:\n", i);
		lh_stats(mh->lh[i]);
	}
}

static void grp_stats(struct heap_elem *e, long sum) {
	struct group *g = (struct group *) e->elem;
	if (g->group_id == DUMMY)
		return;
	printf("%d: runtime %dus sleeptime %dus weight %d ticks %0.2f\n", g->group_id,
	       g->runtime, g->sleeptime,
	       g->weight, 1.0*g->runtime/sum);
}

void mh_runtime_stats(struct mheap *mh) {
	long *ticks = new_ticks();
	ticks_gettime(ticks);
	long sum = ticks_sum(ticks);
	printf("total ticks %dus\n", sum);
	for (int i = 0; i < mh->nheap; i++) {
		for (struct heap_elem *e = heap_first(mh->lh[i]->heap); e != NULL; e = heap_next(mh->lh[i]->heap, e)) {
			grp_stats(e, sum);
		}
	}
}

struct lock_heap *mh_heap(struct mheap *mh, int i) {
	return mh->lh[i];
}

void mh_add_group(struct mheap *mh, struct group *g) {
	int i = random() % mh->nheap;
	struct lock_heap *lh = mh_heap(mh, i);
	g->mh = mh;
	g->lh = lh;
	lh_lock_timed(lh);
	heap_push(lh->heap, &g->heap_elem);
	lh_unlock(lh);
}

void mh_del_group(struct mheap *mh, struct group *g) {
	lh_lock_timed(g->lh);
	g->mh = NULL;
	heap_remove_at(g->lh->heap, &g->heap_elem);
	lh_unlock(g->lh);
}

// to sanity check; run with 1 core
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


// caller must ensure there is a min element
void *mh_min_atomic(struct lock_heap *lh)  {
        struct heap_elem *e = __atomic_load_n(&(lh->heap->heap[0]), __ATOMIC_ACQUIRE);
        return e->elem;
}

// https://dl.acm.org/doi/10.1145/2755573.2755616
struct group *mh_sample_min_group(struct mheap *mh) {
retry:
	int i = random() % mh->nheap;
	int j = random() % mh->nheap;
	while (i == j) {
		j = random() % mh->nheap;
	}
	struct lock_heap *lh_i = mh_heap(mh, i);
	struct lock_heap *lh_j = mh_heap(mh, j);
	struct group *g_i = (struct group *) mh_min_atomic(lh_i);
	struct group *g_j = (struct group *) mh_min_atomic(lh_j);
	if (mh_empty(g_i) && mh_empty(g_j)) {
		return NULL;
	}
	if (mh_empty(g_i)) {
		g_i = g_j;
		lh_i = lh_j;
	} else if (g_j) {
		int svt_i =  __atomic_load_n(&g_i->spec_virt_time, __ATOMIC_SEQ_CST);
		int svt_j =  __atomic_load_n(&g_j->spec_virt_time, __ATOMIC_SEQ_CST);
		if (svt_i > svt_j) {
			g_i = g_j;
			lh_i = lh_j;
		}
	}
	if(lh_try_lock(lh_i) != 0)
		goto retry;
	if ((struct group *) heap_min(lh_i->heap) != g_i) {
		lh_unlock(lh_i);
		goto retry;
	}
	return g_i;
}

// returns with heap locked
struct group *mh_min_group(struct mheap *mh) {
	if (mh->nheap == 1) {
		struct lock_heap *lh = mh_heap(mh, 0);
		lh_lock_timed(lh);
		struct group *g = (struct group *) heap_min(lh->heap);
		if(mh_empty(g)) {
			lh_unlock(lh);
			return NULL;
		}	
		return g;
	}
	return mh_sample_min_group(mh);
}

