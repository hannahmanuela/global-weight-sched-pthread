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

struct mheap *mh_new(int proc_cmp(struct heap_elem *, struct heap_elem *), int n, int tick_length) {
	struct mheap *mh = malloc(sizeof(struct mheap));
	mh->lh = (struct lheap **) aligned_alloc(CACHE_LINE_SZ, sizeof(struct lheap) * n);
	for (int i=0; i < n; i++) {
		mh->lh[i] = lh_new(proc_cmp);
		// insert a dummy element so that the heap always has one elemement
		struct heap_elem* he = malloc(sizeof(struct heap_elem));
		heap_elem_init(he, DUMMY, 0, NULL);
		heap_push(mh->lh[i]->heap, he);
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

static struct heap_elem *mh_min(struct lheap *lh) {
	struct heap_elem *he = heap_min(lh->heap);
	if (he == NULL) {
		printf("heap %d %p %p %p\n", lh->heap->heap_size, lh->heap->heap, &(lh->heap[0]), he);
		assert(0);
	}
	return he;
}

vt_t mh_min_vt(struct lheap *lh) {
	struct heap_elem *min = mh_min(lh);
	vt_t vt = atomic_load(&min->vruntime);
	if (vt == DUMMY)
		return 0;
	return vt;
}

static void print_elem(struct heap_elem *e) {
	if(e->vruntime == DUMMY) {
		printf("[dummy vt %u w %d]", e->vruntime, e->weight);
		return;
	}
	struct process *p = (struct process *) e->elem;
	printf("["); proc_print(p); printf("]");
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
	long a_cycles = 0;
	long a_n = 0;
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
		a_cycles += lh->wait_for_wr_heap_lock_cycles;
		a_n += lh->num_times_wr_heap_locked;
		//lh_stats(lh);
	}
	printf("  cycles: insert l %0.2f h %0.2f remove l %0.2f h %0.2f\n", l_i, h_i, l_r, h_r); 
	printf("  lock cycles l %0.2f a %0.2f h %0.2f\n", l_cycles, AVG(a_cycles,a_n), h_cycles);

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
	heap_push(lh->heap, &p->he);
	int end_tsc = safe_read_tsc();
	lh->insert_cycles += end_tsc - start_tsc;
	p->lh->ninsert += 1;
}

// caller must hold heap lock
struct process *mh_del_min_process(struct lheap *lh) {
	int start_tsc = safe_read_tsc();
	struct heap_elem *he = heap_remove_min(lh->heap);
	int end_tsc = safe_read_tsc();
	lh->remove_cycles += end_tsc - start_tsc;
	lh->nremove += 1;
	return (struct process *) he->elem;
}


// https://dl.acm.org/doi/10.1145/2755573.2755616
struct process *mh_sample_min_group(struct core *c, struct mheap *mh) {
	long start = safe_read_tsc();
	long r = 0;
	long r_lock = 0;
retry:
	int i = c_rand(c, mh->nheap);
	int j = c_rand(c, mh->nheap);
	while (i == j) {
		j = c_rand(c, mh->nheap);
	}
	struct lheap *lh_i = mh_heap(mh, i);
	struct lheap *lh_j = mh_heap(mh, j);
	struct heap_elem *he_i = mh_min(lh_i);
	struct heap_elem *he_j = mh_min(lh_j);
	vt_t vt_i = atomic_load(&he_i->vruntime);
	vt_t vt_j = atomic_load(&he_j->vruntime);
	if ((vt_i == DUMMY) && (vt_j == DUMMY))
		return NULL;
	if (vt_i == DUMMY) {
		vt_i = vt_j;
		he_i = he_j;
		lh_i = lh_j;
	} else {
		if (vt_i > vt_j) {
			vt_i = vt_j;
			he_i = he_j;
			lh_i = lh_j;
		}
		if (vt_i == vt_j) {
			int w_i = atomic_load(&he_i->weight);
			int w_j = atomic_load(&he_j->weight);
			if (w_j > w_i) {	
				vt_i = vt_j;
				he_i = he_j;
				lh_i = lh_j;
			}
		}
	}
	if(lh_try_lock_timed(lh_i) != 0) {
		// printf("%d: retry %d another thread lock acquired heap\n", c->cid, i);
		r++;
		goto retry;
	}
	int vt = heap_min(lh_i->heap)->vruntime;
	if (vt != vt_i) {
		// printf("%d: retry %p not min anymore %d %d ts %ld\n", c->cid, lh_i, vt_i, vt);
		// heap_iter(lh_i->heap, print_elem);  
		r_lock++;
		lh_unlock(lh_i);
		goto retry;
	}
	struct process *p = mh_del_min_process(lh_i);
	lh_unlock(lh_i);
	c->min_proc_cycles += (safe_read_tsc() - start);
	c->nretry_del += (r + r_lock);
	c->nretry_del_lock += r_lock;
	return p;
}

// returns with proc locked
struct process *mh_min_proc(struct core *c, struct mheap *mh) {
	if (mh->nheap == 1) {
		struct lheap *lh = mh_heap(mh, 0);
		lh_lock_timed(lh);
		struct heap_elem *he = mh_min(lh);
		if(he->vruntime == DUMMY) {
			lh_unlock(lh);
			return NULL;
		}	
		struct process *p = mh_del_min_process(lh);
		pthread_rwlock_wrlock(&p->proc_lock);
		assert(p->lh == lh);
		lh_unlock(lh);
		return p;
	}
	return mh_sample_min_group(c, mh);
}
	
