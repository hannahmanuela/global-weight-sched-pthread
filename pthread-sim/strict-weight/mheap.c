#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
#include <limits.h>
#include <float.h>

#include "vt.h"
#include "ticks.h"
#include "driver.h"
#include "core.h"
#include "group.h"
#include "lheap.h"
#include "mheap.h"
#include "util.h"

struct mheap *mh_new(int proc_cmp(void *, void *), int n, int tick_length) {
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
		printf("  Heap %d size %d: \n", i, h->heap_size);
		heap_iter(mh->lh[i]->heap, print_elem);
		printf("\n");
	}
	printf("=\n");
}

void mh_lock_stats(struct mheap *mh) {
	printf("= mh: lock stats:\n");
	float l_i = FLT_MAX;
	float h_i = 0.0;
	float l_r = FLT_MAX;
	float h_r = 0.0;
	float l_cycles = FLT_MAX;
	float h_cycles = 0.0;
	for (int i = 0; i < mh->nheap; i++) {
		struct lheap *lh = mh->lh[i];
		float in = AVG(lh->insert_cycles, lh->ninsert);
		float out = AVG(lh->remove_cycles, lh->nremove);
		l_i = MIN(l_i, in);
		h_i = MAX(h_i, in);
		l_r = MIN(l_r, out);
		h_r = MAX(h_r, out);
		float c = AVG(lh->wait_for_wr_heap_lock_cycles, lh->num_times_wr_heap_locked);
		l_cycles = MIN(l_cycles, c);
		h_cycles = MAX(h_cycles, c);
		//lh_stats(lh);
	}
	printf("  cycles: insert %0.2f %0.2f remove %0.2f %0.2f\n", l_i, h_i, l_r, h_r); 
	printf("  lock cycles %0.2f %0.2f\n", l_cycles, h_cycles);

	printf("=\n");
}

struct lheap *mh_heap(struct mheap *mh, int i) {
	return mh->lh[i];
}

struct lheap *mh_choose_heap(struct core *c, struct mheap *mh) {
	double rand;
	long r = 0;
	if(mh->nheap == 1) {
		struct lheap *lh = mh_heap(mh, 0);
		lh_lock_timed(lh);		
		return lh;
	}
retry:
	int i = c_rand(c, mh->nheap);
	struct lheap *lh = mh_heap(mh, i);
	if(lh_try_lock_timed(lh) != 0) {
		r++;
		goto retry;
	}
	c->nretry_ins += r;
	return lh;
}

// caller must hold heap and proc lock
void mh_add_process(struct process *p, struct lheap *lh) {
	int start_tsc = safe_read_tsc();
	p->lh = lh;
	heap_push(lh->heap, &p->heap_elem);
	int end_tsc = safe_read_tsc();
	lh->insert_cycles += end_tsc - start_tsc;
	p->lh->ninsert += 1;
}

// caller must hold heap and group lock
void mh_del_process(struct mheap *mh, struct process *p) {
	int start_tsc = safe_read_tsc();
	heap_remove_at(p->lh->heap, &p->heap_elem);
	int end_tsc = safe_read_tsc();
	p->lh->remove_cycles += end_tsc - start_tsc;
	p->lh->nremove += 1;
}

// caller must ensure there is a min element
void *mh_min_atomic(struct lheap *lh)  {
        struct heap_elem *e = atomic_load(&(lh->heap->heap[0]));
        return e->elem;
}

// https://dl.acm.org/doi/10.1145/2755573.2755616
struct process *mh_sample_min_group(struct core *c, struct mheap *mh) {
	long start = safe_read_tsc();
	long r = 0;
retry:
	int i = c_rand(c, mh->nheap);
	int j = c_rand(c, mh->nheap);
	while (i == j) {
		j = c_rand(c, mh->nheap);
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
	if(lh_try_lock_timed(lh_i) != 0) {
		r++;
		goto retry;
	}
	if ((struct process *) heap_min(lh_i->heap) != p_i) {
		lh_unlock(lh_i);
		r++;
		goto retry;
	}
	assert(p_i->lh == lh_i);
	mh_del_process(p_i->mh, p_i);
	lh_unlock(lh_i);
	c->min_proc_cycles += (safe_read_tsc() - start);
	c->nretry_del += r;
	return p_i;
}

// returns with proc locked
struct process *mh_min_proc(struct core *c, struct mheap *mh) {
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
	return mh_sample_min_group(c, mh);
}
