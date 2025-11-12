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

extern bool with_tsc;

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

struct lheap *mh_heap(struct mheap *mh, int i) {
	return mh->lh[i];
}

struct lheap *mh_choose_heap(struct core *c, struct mheap *mh) {
	double rand;
	long r = 0;
	if(mh->nheap == 1) {
		struct lheap *lh = mh_heap(mh, 0);
		lh_lock(c, lh);		
		return lh;
	}
retry:
	int i = c_rand(c, mh->nheap);
	struct lheap *lh = mh_heap(mh, i);
	if(lh_try_lock(c, lh) != 0) {
		r++;
		goto retry;
	}
	c->nretry_ins += r;
	return lh;
}

// caller must hold heap and proc lock
void mh_add_process(struct core *c, struct process *p, struct lheap *lh) {
	p->lh = lh;
	heap_push(lh->heap, &p->he);
}

// caller must hold heap lock
struct process *mh_del_min_process(struct core *c, struct lheap *lh) {
	struct heap_elem *he;
	he = heap_remove_min(lh->heap);
	assert(lh->heap->heap_size > 0);  // dummy should stay on heap
	return (struct process *) he->elem;
}

// https://dl.acm.org/doi/10.1145/2755573.2755616
struct process *mh_sample_min_group(struct core *c, struct mheap *mh) {
	long r = 0;
	long r_lock = 0;
retry:
	int i = c_rand(c, mh->nheap);
	int j = c_rand(c, mh->nheap);
	while (i == j) {
		c->nrand++;
		j = c_rand(c, mh->nheap);
	}
	struct lheap *lh_i = mh->lh[i];
	struct lheap *lh_j = mh->lh[j];
	long start = safe_read_tsc();
	struct heap *h_i = lh_i->heap;
	struct heap *h_j = lh_j->heap;
	struct heap_elem *he_i = h_i->heap;
	struct heap_elem *he_j = h_j->heap;
	c->min_proc_cycles += (safe_read_tsc() - start);
	vt_t vt_i = atomic_load(&he_i->vruntime);
	vt_t vt_j = atomic_load(&he_j->vruntime);
	if ((vt_i == DUMMY) && (vt_j == DUMMY)) {
		c->nsched_null += 1;
		return NULL;
	}
	if (vt_i == DUMMY) {
		vt_i = vt_j;
		lh_i = lh_j;
	} else {
		if (vt_i > vt_j) {
			vt_i = vt_j;
			lh_i = lh_j;
		} else if (vt_i == vt_j) {
			int w_i = atomic_load(&he_i->weight);
			int w_j = atomic_load(&he_j->weight);
			if (w_j > w_i) {	
				vt_i = vt_j;
				lh_i = lh_j;
			}
		}
	}
	if(lh_try_lock(c, lh_i) != 0) {
		// printf("%d: retry %d another thread lock acquired heap\n", c->cid, i);
		r++;
		goto retry;
	}
	int vt = heap_min(lh_i->heap)->vruntime;
	if (vt != vt_i) {
		// printf("%d: retry %p not min anymore %d %d ts %ld\n", c->cid, lh_i, vt_i, vt);
		// heap_iter(lh_i->heap, print_elem);  
		r_lock++;
		lh_unlock(c, lh_i);
		goto retry;
	}
	struct process *p = mh_del_min_process(c, lh_i);
	lh_unlock(c, lh_i);
	c->nretry_del += (r + r_lock);
	c->nretry_del_lock += r_lock;
	if(r > c->max_retry_del)
		c->max_retry_del = r;
	if(r_lock > c->max_retry_del_lock)
		c->max_retry_del_lock = r_lock;
	return p;
}

// returns with proc locked
struct process *mh_min_proc(struct core *c, struct mheap *mh) {
	if (mh->nheap == 1) {
		struct lheap *lh = mh_heap(mh, 0);
		lh_lock(c, lh);
		struct heap_elem *he = mh_min(lh);
		if(he->vruntime == DUMMY) {
			lh_unlock(c, lh);
			return NULL;
		}	
		struct process *p = mh_del_min_process(c, lh);
		pthread_rwlock_wrlock(&p->proc_lock);
		assert(p->lh == lh);
		lh_unlock(c, lh);
		return p;
	}
	return mh_sample_min_group(c, mh);
}
	
