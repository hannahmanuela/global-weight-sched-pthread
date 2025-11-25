#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
#include <limits.h>
#include <float.h>

#include "vt.h"
#include "driver.h"
#include "core.h"
#include "group.h"
#include "heap.h"
#include "mheap.h"
#include "util.h"

#define W_DUMMY 0

struct mheap *mh_new(int n) {
	struct mheap *mh = malloc(sizeof(struct mheap));
	mh->h = (struct heap **) aligned_alloc(CACHE_LINE_SZ, sizeof(struct heap) * n);
	for (int i=0; i < n; i++) {
		mh->h[i] = heap_new();
		mh->h[i]->id = i;
		// insert a dummy element so that the heap always has one elemement
		struct heap_elem* he = malloc(sizeof(struct heap_elem));
		heap_elem_init(he, DUMMY, W_DUMMY);
		heap_push(mh->h[i], he);
	}
	mh->nheap = n;
	return mh;
}

void mh_free(struct mheap *mh) {
	for (int i = 0; i < mh->nheap; i++) {
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
		assert(min <= h->heap[i]->vruntime);
	}
}

static void print_elem(struct heap_elem *e) {
	if(e->vruntime == DUMMY) {
		printf("[dummy vt %u w %d idx %d]", e->vruntime, e->weight, e->idx);
		return;
	}
	struct process *p = container_of(e, struct process, he);
	printf("["); proc_print(p); printf("]");
}

void mh_print_min(struct mheap *mh) {
	printf("= mh min:\n");
	for (int i = 0; i < mh->nheap; i++) {
		struct heap *h = mh->h[i];
		printf("%d(%d): ", i, h->heap_size);
		print_elem(h->heap[0]);
		printf("\n");
	}
	printf("=\n");
}

void mh_print(struct mheap *mh) {
	printf("= mh:\n");
	for (int i = 0; i < mh->nheap; i++) {
		struct heap *h = mh->h[i];
		printf("  Heap %d size %d: \n", i, h->heap_size);
		heap_iter(mh->h[i], print_elem);
		printf("\n");
	}
	printf("=\n");
}

struct heap *mh_choose_heap(struct mheap *mh, struct core *c) {
	long r = 0;
	if(mh->nheap == 1) {
		struct heap *h = mh->h[0];
		lock_acquire(&h->lk, c);		
		return h;
	}
retry:
	int i = c_rand(c, mh->nheap);
	struct heap *h = mh->h[i];
	if(lock_try_acquire(&h->lk, c) != 0) {
		r++;
		goto retry;
	}
	c->nretry_ins += r;
	return h;
}

// caller must hold heap lock
void mh_add_process(struct core *c, struct process *p, struct heap *h) {
	if(h->heap_size + 1 >= h->heap_capacity) {
		mh_print(p->mh);
	}
	heap_push(h, &p->he);
	p->h = h;
	lock_release(&h->lk, c);
}

// caller must hold heap lock
static struct process *mh_remove_min(struct heap *h) {
	struct heap_elem *he = heap_remove_min(h);
	assert(h->heap_size > 0);  // dummy should stay on heap
	return container_of(he, struct process, he);
}

// caller must hold heap lock
static struct process *mh_del_min_process(struct core *c, struct heap *h) {
	struct process *p = mh_remove_min(h);
	lock_acquire(&p->lk, c);
	p->he.idx = -1;
	p->cid = c->cid;
	lock_release(&p->lk, c);
	return p;
}

static void  __attribute__ ((noinline)) mh_rand_heaps(struct mheap *mh, struct core *c, int *i, int *j) {
	*i = c_rand(c, mh->nheap);
	*j = c_rand(c, mh->nheap);
	while (*i == *j) {
		c->nrand++;
		*j = c_rand(c, mh->nheap);
	}
}

static void __attribute__ ((noinline)) mh_rand_heap(struct mheap *mh, struct core *c, int i, int *j) {
	*j = c_rand(c, mh->nheap);
	while (i == *j) {
		c->nrand++;
		*j = c_rand(c, mh->nheap);
	}
}

static void mh_upd_stat(struct process *p, struct core *c, int other, vt_t other_vt, int r, int r_lock) {
	p->other_hid = other;
	p->other_vt = other_vt;
	c->nretry_del += (r + r_lock);
	c->nretry_del_lock += r_lock;
	if(r > c->max_retry_del)
		c->max_retry_del = r;
	if(r_lock > c->max_retry_del_lock)
		c->max_retry_del_lock = r_lock;
}

static struct heap  __attribute__ ((noinline)) *mh_select_affinity(struct mheap *mh, struct core *c, int i, int j, vt_t *vt, vt_t *other_vt) {
	vt_t ovt;
	struct heap *h_i = mh->h[i];
	struct heap *h_j = mh->h[j];
	struct heap_elem *he_i = h_i->heap[0];
	struct heap_elem *he_j = h_j->heap[0];
	vt_t vt_i = atomic_load_explicit(&he_i->vruntime, __ATOMIC_RELAXED);
	vt_t vt_j = atomic_load_explicit(&he_j->vruntime, __ATOMIC_RELAXED);
	int w_i = atomic_load_explicit(&he_i->weight, __ATOMIC_RELAXED);
	int w_j = atomic_load_explicit(&he_j->weight, __ATOMIC_RELAXED);
	if (w_j == W_DUMMY) {
		assert(vt_j == DUMMY);
		ovt = DUMMY;
	} else if (w_i == w_j) {
		ovt = vt_j;
		//} else if (w_i > w_j) {
		// printf("%d(%d) %d(%d):", i, vt_i, j, vt_j); mh_print_min(mh);
		//ovt = vt_j;
	} else {
		if (vt_i > vt_j) {
			ovt = vt_i;
			vt_i = vt_j;
			h_i = h_j;
		} else {
			ovt = vt_j;
		}
	}
	*vt = vt_i;
	*other_vt = ovt;
	return h_i;
}

static struct heap  __attribute__ ((noinline)) *mh_select(struct mheap *mh, struct core *c, int i, int j, vt_t *vt, vt_t *other_vt) {
	vt_t ovt;
	struct heap *h_i = mh->h[i];
	struct heap *h_j = mh->h[j];
	vt_t vt_i = atomic_load_explicit(&h_i->heap[0]->vruntime, __ATOMIC_RELAXED);
	vt_t vt_j = atomic_load_explicit(&h_j->heap[0]->vruntime, __ATOMIC_RELAXED);
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
			struct heap_elem *he_i = h_i->heap[0];
			struct heap_elem *he_j = h_j->heap[0];
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
static struct process *mh_sample_min_proc(struct mheap *mh, struct core *c) {
	long r = 0;
	long r_lock = 0;
retry:
	int i, j;
	vt_t vt;
	vt_t other_vt;

	mh_rand_heaps(mh, c, &i, &j);
	struct heap *h = mh_select(mh, c, i, j, &vt, &other_vt);
	if (h == NULL) {
		c->nsched_null += 1;
		return NULL;
	}

	int l = lock_try_acquire(&h->lk, c);
	if (l != 0) {
		r++;
		goto retry;
	}
	vt_t vt0 = h->heap[0]->vruntime;
	// vt_t vt0 = h->min_vt;
	if (vt != vt0) {
		r_lock++;
		lock_release(&h->lk, c);
		goto retry;
	}
	struct process *p = mh_del_min_process(c, h);
	lock_release(&h->lk, c);
	mh_upd_stat(p, c, (h->id == i) ? j  : i, other_vt, r, r_lock); 
	return p;
}

struct process *mh_min_proc(struct mheap *mh, struct core *c) {
	if (mh->nheap == 1) {
		struct heap *h = mh->h[0];
		lock_acquire(&h->lk, c);
		struct heap_elem *he = mh_min(h);
		if(he->vruntime == DUMMY) {
			lock_release(&h->lk, c);
			return NULL;
		}	
		struct process *p = mh_del_min_process(c, h);
		assert(p->h == h);
		lock_release(&h->lk, c);
		return p;
	}
	return mh_sample_min_proc(mh, c);
}
	
struct process *mh_min_affinity(struct core *c) {
	long r = 0;
	long r_lock = 0;
	struct process *cp = c->process;
	struct heap *h = cp->h;
	struct process *p = NULL;
	lock_acquire(&h->lk, c);
	lock_acquire(&cp->lk, c);
	if (cp->cid != c->cid) {  // some other core is running cp or has run it
		c->miss[cp->group->gid]++;
		goto end;
	}
	assert(cp->he.idx >= 0);
	if(cp->he.idx > 0) {
		c->miss[cp->group->gid]++;
		goto end;
	}
retry:
	int j;
	vt_t vt;
	vt_t other_vt;
	mh_rand_heap(cp->mh, c, h->id, &j);
	struct heap *h1 = mh_select_affinity(cp->mh, c, h->id, j, &vt, &other_vt);
	if (h1 == h) {
		// printf("hit %d(%d) %d(%d):", h->id, vt, j, other_vt);
		// mh_print_min(cp->mh);
		c->hit[cp->group->gid]++;
		p = mh_remove_min(h);
		assert(cp == p);
		assert(cp->cid == p->cid);
		p->he.idx = -1;
		mh_upd_stat(p, c, j, other_vt, r, r_lock);
		goto end;
	}
	int l = lock_try_acquire(&h1->lk, c);
	if (l != 0) {
		r++;
		goto retry;
	}
	vt_t vt0 = h1->heap[0]->vruntime;
	if (vt != vt0) {
		r_lock++;
		lock_release(&h1->lk, c);
		goto retry;
	}	
	c->miss[cp->group->gid]++;
	p = mh_del_min_process(c, h1);
	lock_release(&h1->lk, c);
	mh_upd_stat(p, c, j, other_vt, r, r_lock);
end:
	lock_release(&cp->lk, c);
	lock_release(&h->lk, c);
	return p;
}
