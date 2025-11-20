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
#include "heap.h"
#include "mheap.h"
#include "util.h"

extern bool with_tsc;

struct mheap *mh_new(int proc_cmp(struct heap_elem *, struct heap_elem *), int n, int tick_length) {
	struct mheap *mh = malloc(sizeof(struct mheap));
	mh->h = (struct heap **) aligned_alloc(CACHE_LINE_SZ, sizeof(struct heap) * n);
	for (int i=0; i < n; i++) {
		mh->h[i] = heap_new(proc_cmp);
		mh->h[i]->id = i;
		// insert a dummy element so that the heap always has one elemement
		struct heap_elem* he = malloc(sizeof(struct heap_elem));
		heap_elem_init(he, DUMMY, 0, NULL);
		heap_push(mh->h[i], he);
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
		heap_iter(mh->h[i], mh_free_item);
		heap_free(mh->h[i]);
	}
}

static struct heap_elem *mh_min(struct heap *h) {
	struct heap_elem *he = heap_min(h);
	if (he == NULL) {
		assert(0);
	}
	return he;
}

vt_t mh_min_vt(struct heap *h) {
	struct heap_elem *min = mh_min(h);
	vt_t vt = atomic_load(&min->vruntime);
	if (vt == DUMMY)
		return 0;
	return vt;
}

static vt_t heap_check(struct heap *h) {
	vt_t min = mh_min_vt(h);
	for (int i = 0; i < h->heap_size; i++) {
		assert(min <= h->heap[i].vruntime);
	}
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
		struct heap *h = mh->h[i];
		printf("  Heap %d size %d: \n", i, h->heap_size);
		heap_iter(mh->h[i], print_elem);
		printf("\n");
	}
	printf("=\n");
}

struct heap *mh_choose_heap(struct core *c, struct mheap *mh) {
	long r = 0;
	if(mh->nheap == 1) {
		struct heap *h = mh->h[0];
		lock_acquire(&h->lk);		
		return h;
	}
retry:
	int i = c_rand(c, mh->nheap);
	struct heap *h = mh->h[i];
	if(lock_try_acquire(&h->lk) != 0) {
		r++;
		goto retry;
	}
	c->nretry_ins += r;
	return h;
}

// caller must hold heap and proc lock
void mh_add_process(struct core *c, struct process *p, struct heap *h) {
	p->h = h;
	heap_push(h, &p->he);
	// heap_check(h);
	lock_release(&p->h->lk);
}

// caller must hold heap lock
struct process *mh_del_min_process(struct core *c, struct heap *h) {
	struct heap_elem *he;
	he = heap_remove_min(h);
	assert(h->heap_size > 0);  // dummy should stay on heap
	return (struct process *) he->elem;
}

static void  __attribute__ ((noinline)) mh_rand_heaps(struct core *c, struct mheap *mh, int *i, int *j) {
	*i = c_rand(c, mh->nheap);
	*j = c_rand(c, mh->nheap);
	while (*i == *j) {
		c->nrand++;
		*j = c_rand(c, mh->nheap);
	}
}

static struct heap  __attribute__ ((noinline)) *mh_select(struct core *c, struct mheap *mh, int i, int j, vt_t *vt, vt_t *other_vt) {
	vt_t ovt;
	struct heap *h_i = mh->h[i];
	struct heap *h_j = mh->h[j];
	vt_t vt_i = atomic_load_explicit(&h_i->heap->vruntime, __ATOMIC_RELAXED);
	vt_t vt_j = atomic_load_explicit(&h_j->heap->vruntime, __ATOMIC_RELAXED);
	if ((vt_i == DUMMY) && (vt_j == DUMMY)) {
		return NULL;
	}
	if (vt_i == DUMMY) {
		vt_i = vt_j;
		h_i = h_j;
		ovt = vt_i;
	} else {
		if (vt_i > vt_j) {
			ovt = vt_i;
			vt_i = vt_j;
			h_i = h_j;
		} else if (vt_i == vt_j) {
			ovt = vt_i;
			struct heap_elem *he_i = h_i->heap;
			struct heap_elem *he_j = h_j->heap;
			int w_i = atomic_load_explicit(&he_i->weight, __ATOMIC_RELAXED);
			int w_j = atomic_load_explicit(&he_j->weight, __ATOMIC_RELAXED);
			if (w_j > w_i) {	
				vt_i = vt_j;
				h_i = h_j;
			}
		}
	}
	*vt = vt_i;
	*other_vt = ovt;
	return h_i;
}

// https://dl.acm.org/doi/10.1145/2755573.2755616
struct process *mh_sample_min_proc(struct core *c, struct mheap *mh) {
	long r = 0;
	long r_lock = 0;
retry:
	int i, j;
	vt_t vt;
	vt_t other_vt;

	mh_rand_heaps(c, mh, &i, &j);
	struct heap *h = mh_select(c, mh, i, j, &vt, &other_vt);
	if (h == NULL) {
		c->nsched_null += 1;
		return NULL;
	}

        // if (lock_holding(&h->lk)) goto retry;

	int l = lock_try_acquire(&h->lk);
	if (l != 0) {
		r++;
		goto retry;
	}
	vt_t vt0 = h->heap->vruntime;
	// vt_t vt0 = h->min_vt;
	if (vt != vt0) {
		r_lock++;
		lock_release(&h->lk);
		goto retry;
	}
	struct process *p = mh_del_min_process(c, h);
	lock_release(&h->lk);
	p->other_hid = (h->id == i) ? j : i;
	p->other_vt = other_vt;
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
		struct heap *h = mh->h[0];
		lock_acquire(&h->lk);
		struct heap_elem *he = mh_min(h);
		if(he->vruntime == DUMMY) {
			lock_release(&h->lk);
			return NULL;
		}	
		struct process *p = mh_del_min_process(c, h);
		assert(p->h == h);
		lock_release(&h->lk);
		return p;
	}
	return mh_sample_min_proc(c, mh);
}
	
