#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
#include <limits.h>

#include "vt.h"
#include "driver.h"
#include "group.h"
#include "lheap.h"
#include "mheap.h"

struct mheap *mh_new(int proc_cmp(void *, void *), int n, int seed, int tick_length) {
	srandom(seed);
	struct mheap *mh = malloc(sizeof(struct mheap));
	mh->lh = (struct lheap **) malloc(sizeof(struct lheap) * n);
	for (int i=0; i < n; i++) {
		mh->lh[i] = lh_new(proc_cmp);
		// insert a dummy element so that the heap always has one elemement
		struct process* dummy = grp_new_process(mh, DUMMY, NULL);
		dummy->vruntime = INT_MAX;
		heap_push(mh->lh[i]->heap, &dummy->heap_elem);
	}
	mh->nheap = n;
	mh->tick_length = tick_length;
	return mh;
}

static void mh_free_item(struct heap_elem *e) {
	free(e->elem);
}

void mh_free(struct mheap *mh) {
	for (int i = 0; i < mh->nheap; i++) {
		heap_iter(mh->lh[i]->heap, mh_free_item);
		heap_free(mh->lh[i]->heap);
	}
}

int mh_min(struct lheap *lh) {
	struct process *min = (struct process *) heap_min(lh->heap);
	long mvt = 0;
	if (min && !proc_dummy(min)) {
		mvt = min->vruntime;
	}	
	return mvt;
}

static void print_elem(struct heap_elem *e) {
	struct process *p = (struct process *) e->elem;
	printf("[%d: ", e->heap_index); proc_print(p); printf("]");
}

void mh_print(struct mheap *mh) {
	printf("= mh tl %d\n", mh->tick_length);
	for (int i = 0; i < mh->nheap; i++) {
		struct heap *h = mh->lh[i]->heap;
		printf("Heap %d size %d: \n", i, h->heap_size);
		heap_iter(mh->lh[i]->heap, print_elem);
		printf("\n");
	}
	printf("=\n");
}

void mh_lock_stats(struct mheap *mh) {
	printf("= mh: lock stats: retry insert %d retry remove %d\n", mh->nretry_insert, mh->nretry_remove);
	for (int i = 0; i < mh->nheap; i++) {
		printf("== heap %d:\n", i);
		lh_stats(mh->lh[i]);
	}
	printf("=\n");
}

struct lheap *mh_heap(struct mheap *mh, int i) {
	return mh->lh[i];
}

struct lheap *mh_choose_heap(struct mheap *mh) {
retry:
	int i = random() % mh->nheap;
	struct lheap *lh = mh_heap(mh, i);
	if(lh_try_lock(lh) != 0) {
		atomic_fetch_add(&mh->nretry_insert, 1);
		goto retry;
	}
	return lh;
}

// caller must hold heap and proc lock
void mh_add_process(struct process *p, struct lheap *lh) {
	p->lh = lh;
	heap_push(lh->heap, &p->heap_elem);
}

// caller must hold heap and group lock
void mh_del_process(struct mheap *mh, struct process *p) {
	heap_remove_at(p->lh->heap, &p->heap_elem);
}

// caller must ensure there is a min element
void *mh_min_atomic(struct lheap *lh)  {
        struct heap_elem *e = atomic_load(&(lh->heap->heap[0]));
        return e->elem;
}

// https://dl.acm.org/doi/10.1145/2755573.2755616
struct process *mh_sample_min_group(struct mheap *mh) {
retry:
	int i = random() % mh->nheap;
	int j = random() % mh->nheap;
	while (i == j) {
		j = random() % mh->nheap;
	}
	struct lheap *lh_i = mh_heap(mh, i);
	struct lheap *lh_j = mh_heap(mh, j);
	struct process *p_i = (struct process *) mh_min_atomic(lh_i);
	struct process *p_j = (struct process *) mh_min_atomic(lh_j);
	if (proc_dummy(p_i) && proc_dummy(p_j)) {
		return NULL;
	}
	if (proc_dummy(p_i)) {
		p_i = p_j;
		lh_i = lh_j;
	} else if (p_j) {
		int vt_i = atomic_load(&p_i->vruntime);
		int vt_j = atomic_load(&p_j->vruntime);
		if (vt_i > vt_j) {
			p_i = p_j;
			lh_i = lh_j;
		}
		if (vt_i == vt_j) {
			int w_i = atomic_load(&p_i->weight);
			int w_j = atomic_load(&p_j->weight);
			if (w_j > w_i) {	
				p_i = p_j;
				lh_i = lh_j;
			}
		}
	}
	if(lh_try_lock(lh_i) != 0) {
		atomic_fetch_add(&mh->nretry_remove, 1);
		goto retry;
	}
	if ((struct process *) heap_min(lh_i->heap) != p_i) {
		lh_unlock(lh_i);
		atomic_fetch_add(&mh->nretry_remove, 1);
		goto retry;
	}
	pthread_rwlock_wrlock(&p_i->proc_lock);
	assert(p_i->lh == lh_i);
	mh_del_process(p_i->mh, p_i);
	lh_unlock(lh_i);
	return p_i;
}

// returns with proc locked
struct process *mh_min_proc(struct mheap *mh) {
	if (mh->nheap == 1) {
		struct lheap *lh = mh_heap(mh, 0);
		lh_lock_timed(lh);
		struct process *p = (struct process *) heap_min(lh->heap);
		if(!p || proc_dummy(p)) {
			lh_unlock(lh);
			return NULL;
		}	
		pthread_rwlock_wrlock(&p->proc_lock);
		assert(p->lh == lh);
		mh_del_process(p->mh, p);
		lh_unlock(lh);
		return p;
	}
	return mh_sample_min_group(mh);
}
