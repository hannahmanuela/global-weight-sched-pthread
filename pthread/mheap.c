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

//
// concurrent multiheap inspired by https://dl.acm.org/doi/10.1145/2755573.2755616
//

#define W_DUMMY 0

#define MH_IND(mh, i) ((i) % mh->nheap)

extern bool do_affinity;
extern bool use_power2_insert;
extern bool debug;

struct mheap *mh_new(int n) {
	struct mheap *mh = malloc(sizeof(struct mheap));
	mh->h = (struct heap **) aligned_alloc(CACHE_LINE_SZ, sizeof(struct heap) * n);
	for (int i=0; i < n; i++) {
		mh->h[i] = heap_new();
		mh->h[i]->id = i;
		// insert a dummy element so that the heap always has one elemement
		struct heap_elem* he = malloc(sizeof(struct heap_elem));
		heap_elem_init(he, DUMMY, W_DUMMY, NULL);
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

struct heap *mh_heap(struct mheap *mh, int i) {
	return mh->h[i];
}

static struct heap_elem *mh_min(struct heap *h) {
	struct heap_elem *he = heap_min(h);
	assert(he != NULL);
	return he;
}

vt_t mh_min_vt(struct heap *h) {
	struct heap_elem *min = mh_min(h);
	vt_t vt = atomic_load(&min->vruntime);
	return vt;
}

vt_t mh_last_vt(struct heap *h) {
	return atomic_load_explicit(&h->last_vt, __ATOMIC_RELAXED);
}

static vt_t heap_check(struct heap *h) {
	vt_t min = mh_min_vt(h);
	for (int i = 0; i < h->heap_size; i++) {
		assert(min <= h->heap[i].vruntime);
	}
}

void mh_stats(struct mheap *mh) {
	int max = 0;
	int n = 0;
	for (int i = 0; i < mh->nheap; i++) {
		n += mh->h[i]->max;
		if (mh->h[i]->max > max)
			max = mh->h[i]->max;
	}
	printf("mh_stats: max %d %0.2f\n", max, AVG(n, mh->nheap));
}

static void print_elem(struct heap_elem *e) {
	if(e->vruntime == DUMMY) {
		printf("[dummy vt %lld w %d]", e->vruntime, e->weight);
		return;
	}
	struct task_struct *p = (struct task_struct *) e->elem;
	printf("("); proc_print(p); printf(")");
}

void mh_print_min(struct mheap *mh) {
	printf("= mh min:\n");
	for (int i = 0; i < mh->nheap; i++) {
		struct heap *h = mh->h[i];
		printf("%d(%d): ", i, h->heap_size);
		print_elem(&h->heap[0]);
		printf("\n");
	}
	printf("=\n");
}

void mh_print(struct mheap *mh) {
	printf("= mh:\n");
	for (int i = 0; i < mh->nheap; i++) {
		struct heap *h = mh->h[i];
		printf("  Heap %d size %d last_vt %lld: \n", i, h->heap_size, h->last_vt);
		heap_iter(mh->h[i], print_elem);
		printf("\n");
	}
	printf("=\n");
}

float mh_load(struct mheap *mh, int *maxl) {
	long tot = 0;
	for (int i = 0; i < mh->nheap; i++) {
		struct heap *h = mh->h[i];
		tot += h->heap_size;
		if(h->heap_size > *maxl)
			*maxl = h->heap_size;
	}
	return ((float) tot)/mh->nheap;
}

void  __attribute__ ((noinline)) mh_rand_heaps(struct mheap *mh, int *i, int *j) {
	*i = c_rand(mh->nheap);
	*j = c_rand(mh->nheap);
	while (*i == *j) {
		get_core()->nrand++;
		*j = c_rand(mh->nheap);
	}
}

static void __attribute__ ((noinline)) mh_rand_heap(struct mheap *mh, int i, int *j) {
	*j = c_rand(mh->nheap);
	while (i == *j) {
		get_core()->nrand++;
		*j = c_rand(mh->nheap);
	}
}

static int mh_least_loaded(struct mheap *mh, int i, int j) {
	int s1 = atomic_load_explicit(&(mh->h[i]->heap_size), __ATOMIC_ACQUIRE);
	int s2 = atomic_load_explicit(&(mh->h[j]->heap_size), __ATOMIC_ACQUIRE);
	if(s2 < s1) return j;
	else return i;
}

struct heap *mh_choose_heap(struct mheap *mh) {
	long r = 0;
	if(mh->nheap == 1) {
		struct heap *h = mh->h[0];
		lock_acquire(&h->lk);
		return h;
	}
retry:
	int i;
	if(use_power2_insert) {
		int j;
		mh_rand_heaps(mh, &i, &j);
		i = mh_least_loaded(mh, i, j);
	} else {
		i = c_rand(mh->nheap);
	}

	struct heap *h = mh->h[i];
	if(lock_try_acquire(&h->lk) != 0) {
		r++;
		goto retry;
	}
	get_core()->nretry_ins += r;
	return h;
}

// caller must hold heap lock
void mh_add_process(struct task_struct *p, struct heap *h) {
	p->h = h;
	assert(p->mh != NULL);
	heap_push(h, &p->he);
	if (debug) {
		printf("%d(%d): add to heap %d\n", p->pid, p->group->gid, h->id);
	}
}

// caller must hold heap lock
static struct task_struct *mh_remove_min(struct heap *h) {
	struct heap_elem *he = heap_remove_min(h);
	assert(h->heap_size > 0);  // dummy should stay on heap
	return (struct task_struct *) he->elem;
}

// caller must hold heap lock
static struct task_struct *mh_del_min_process(struct heap *h) {
	struct task_struct *p = mh_remove_min(h);
	if(do_affinity) {
		struct core *c = get_core();
		atomic_store_explicit(&p->cid, c->cid, __ATOMIC_RELAXED);
	}
	return p;
}

static void mh_upd_stat(struct task_struct *p, int other, vt_t vt, vt_t other_vt, int r, int r_lock) {
	struct core *c = get_core();

	p->other_hid = other;
	p->other_vt = other_vt;
	c->nretry_del += (r + r_lock);
	c->nretry_del_lock += r_lock;
	if(r > c->max_retry_del)
		c->max_retry_del = r;
	if(r_lock > c->max_retry_del_lock)
		c->max_retry_del_lock = r_lock;
}

static struct heap  __attribute__ ((noinline)) *mh_select(struct mheap *mh, int i, int j, vt_t *vt, vt_t *other_vt) {
	vt_t ovt;
	struct heap *h_i = mh->h[i];
	struct heap *h_j = mh->h[j];
	vt_t vt_i = atomic_load_explicit(&h_i->heap[0].vruntime, __ATOMIC_RELAXED);
	vt_t vt_j = atomic_load_explicit(&h_j->heap[0].vruntime, __ATOMIC_RELAXED);
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
			struct heap_elem *he_i = &h_i->heap[0];
			struct heap_elem *he_j = &h_j->heap[0];
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

// del min proc from h; may fail because some other core grabbed the min vt
static struct task_struct  __attribute__ ((noinline)) *mh_try_del_min(struct heap *h, vt_t vt) {
	int l = lock_try_acquire(&h->lk);
	if (l != 0) {
		return NULL;
	}
	vt_t vt0 = h->heap[0].vruntime;
	// vt_t vt0 = h->min_vt;
	if (vt != vt0) {
		lock_release(&h->lk);
		return NULL;
	}
	struct task_struct *p = mh_del_min_process(h);
	p->tsc = safe_read_tsc();
	return p;
}

static bool mh_keep_running_proc(vt_t vt0, int w, struct task_struct *curp) {
	if (curp == NULL)
		return false;
	if (vt0 == DUMMY)
		return true;
	if (vt0 < curp->he.vruntime)
		return false;
	if ((vt0 == curp->he.vruntime) && (w > curp->group->weight))
		return false;
	return true;
}

// caller must have h locked
static struct task_struct *mh_keep_running_or_switch(struct heap *h, vt_t vt, int w, struct task_struct *to_add) {
	struct task_struct *p = NULL;
	if (mh_keep_running_proc(vt, h->heap[0].weight, to_add)) {
		// pretend we added and removed to_add from the heap
		h->last_vt = to_add->he.vruntime;
		to_add->h = h;
		p = to_add;
	} else if (vt != DUMMY) { 
		p = mh_del_min_process(h);
		assert(p != NULL);
		if (to_add != NULL)  {
			get_core()->ndelay_yield++;
			mh_add_process(to_add, h);
		}
	}
	return p;
}

static struct task_struct  __attribute__ ((noinline)) *mh_try_del_min_enq_prev(struct heap *h, vt_t vt, struct task_struct *to_add) {
	int l = lock_try_acquire(&h->lk);
	if (l != 0) {
		return NULL;
	}
	vt_t vt0 = h->heap[0].vruntime;
	// vt_t vt0 = h->min_vt;
	if (vt != vt0) {
		lock_release(&h->lk);
		return NULL;
	}
	struct task_struct *p = mh_keep_running_or_switch(h, vt, h->heap[0].weight, to_add);
	lock_release(&h->lk);
	return p;
}

static struct task_struct  __attribute__ ((noinline)) *mh_all_min_proc(struct mheap *mh, int s) {
	struct task_struct *p = NULL;
	for (int i = 0; i < mh->nheap; i++) {
		struct heap *h = mh->h[MH_IND(mh, i+s)];
		vt_t vt = atomic_load_explicit(&h->heap[0].vruntime, __ATOMIC_RELAXED);
		if (vt != DUMMY && ((p = mh_try_del_min(h, vt)) != NULL)) {
			break;
		}
	}
	return p;
}

static struct task_struct  __attribute__ ((noinline)) *mh_sample_min_proc_enq(struct mheap *mh, struct task_struct *curp, bool all) {
	long r = 0;
	long r_lock = 0;  // XXX delete?
	struct task_struct *p;
	struct heap *h;
	int i, j;
	vt_t vt;
	vt_t other_vt;

	while(true) {
		p = NULL;
		mh_rand_heaps(mh, &i, &j);
		if ((h = mh_select(mh, i, j, &vt, &other_vt)) == NULL) {
			if(all) p = mh_all_min_proc(mh, i);
			break;
		} 
		if ((p = mh_try_del_min_enq_prev(h, vt, curp)) != NULL) {
			curp = NULL;
			break;
		}
		r++;
	}

	if(p != NULL) {
		// h could be NULL after mh_all_min_proc
		mh_upd_stat(p, (h && (h->id == i)) ? j  : i, vt, other_vt, r, r_lock); 
	}

	if ((p != NULL) && (curp != NULL)) {
		i = mh_least_loaded(mh, i, j);
		struct heap *h = mh->h[i];
		if(lock_try_acquire(&h->lk) != 0) {
			h = mh_choose_heap(mh);
		}
		mh_add_process(curp, h);
		lock_release(&h->lk);
	} else {
		// XXX pretend we added and removed to_add from the heap
		// h->last_vt = to_add->vruntime;
	}
	return p;
}

struct task_struct *mh_min_proc_one_heap(struct mheap *mh, struct task_struct *to_add) {
	struct heap *h = mh->h[0];

	lock_acquire(&h->lk);
	struct heap_elem *he = mh_min(h);
	struct task_struct *p = mh_keep_running_or_switch(h, he->vruntime, he->weight, to_add);
	lock_release(&h->lk);
	return p;
}

struct task_struct *mh_min_proc(struct mheap *mh, bool all) {
	if (mh->nheap == 1) {
		return mh_min_proc_one_heap(mh, NULL);
	}
	return mh_sample_min_proc_enq(mh, NULL, all);
}

// if there is a min, grab it and enqueue p
struct task_struct *mh_min_proc_enq(struct mheap *mh, struct task_struct *to_add, bool all) {
	if (mh->nheap == 1) {
		return mh_min_proc_one_heap(mh, to_add);
	}
	return mh_sample_min_proc_enq(mh, to_add, all);
}

//
// schedule with affinity: remember last process run on a core; if the core
// sees it later at the front of the heap, it selects it, if another random queue
// has a process of the same weight at the front (or no process at all).
//

static struct heap  __attribute__ ((noinline)) *mh_select_affinity(struct mheap *mh, struct core *c, int i, int j, vt_t *vt, vt_t *other_vt) {
	vt_t ovt;
	struct heap *h_i = mh->h[i];
	struct heap *h_j = mh->h[j];
	struct heap_elem *he_i = &(h_i->heap[0]);
	struct heap_elem *he_j = &(h_j->heap[0]);
	vt_t vt_i = atomic_load_explicit(&he_i->vruntime, __ATOMIC_RELAXED);
	vt_t vt_j = atomic_load_explicit(&he_j->vruntime, __ATOMIC_RELAXED);
	int w_i = atomic_load_explicit(&he_i->weight, __ATOMIC_RELAXED);
	int w_j = atomic_load_explicit(&he_j->weight, __ATOMIC_RELAXED);
	if (vt_j == DUMMY) {
		ovt = DUMMY;
	} else if (w_i == w_j) {
		ovt = vt_j;
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

struct task_struct *mh_min_affinity(struct core *c) {
	struct task_struct *cp = c->process;
	struct heap *h = cp->h;
	int cid = atomic_load_explicit(&cp->cid, __ATOMIC_RELAXED);
	if (cid != c->cid) {  // some other core is running cp or has run it
		c->miss[cp->group->gid]++;
		return NULL;
	}
	if(atomic_load_explicit(&h->heap[0].elem, __ATOMIC_RELAXED) != cp) {
		c->miss[cp->group->gid]++;
		return NULL;
	}
	struct task_struct *p = NULL;
	lock_acquire(&h->lk);
	if((h->heap[0].elem != cp) || (cp->cid != c->cid)) {
		c->miss[cp->group->gid]++;
		goto end;
	}
	long r = 0;
	long r_lock = 0;
retry:
	int j;
	vt_t vt;
	vt_t other_vt;
	mh_rand_heap(cp->mh, h->id, &j);
	struct heap *h1 = mh_select_affinity(cp->mh, c, h->id, j, &vt, &other_vt);
	if (h1 == h) {
		// printf("hit %d(%d) %d(%d):", h->id, vt, j, other_vt);
		// mh_print_min(cp->mh);
		c->hit[cp->group->gid]++;
		p = mh_remove_min(h);
		assert(cp == p);
		assert(p->cid == cid);
		mh_upd_stat(p, j, vt, other_vt, r, r_lock);
		goto end;
	}
	int l = lock_try_acquire(&h1->lk);
	if (l != 0) {
		r++;
		goto retry;
	}
	vt_t vt0 = h1->heap[0].vruntime;
	if (vt != vt0) {
		r_lock++;
		lock_release(&h1->lk);
		goto retry;
	}	
	c->miss[cp->group->gid]++;
	p = mh_del_min_process(h1);
	lock_release(&h1->lk);
	mh_upd_stat(p, j, vt, other_vt, r, r_lock);
end:
	lock_release(&h->lk);
	return p;
}
