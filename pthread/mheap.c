#define _GNU_SOURCE

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
#include <limits.h>
#include <float.h>

#include "vt.h"
#include "driver.h"
#include "core.h"
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
	mh->h = (struct heap **) aligned_alloc(CACHE_LINE_SZ, ALIGN_UP(sizeof(struct heap) * n, CACHE_LINE_SZ));
	for (int i=0; i < n; i++) {

		mh->h[i] = heap_new();
		mh->h[i]->id = i;
		lock_init(&(mh->h[i]->lk));
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
		assert(min <= h->heap[i]->vruntime);
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

void mh_print_min(struct mheap *mh, void (*print_heap_elem)(struct heap_elem *)) {
	printf("= mh min:\n");
	for (int i = 0; i < mh->nheap; i++) {
		struct heap *h = mh->h[i];
		lock_acquire(&h->lk);
		printf("%d(%d): ", i, h->heap_size);
		print_heap_elem(h->heap[0]);
		printf("\n");
		lock_release(&h->lk);
	}
	printf("=\n");
}

void mh_print(struct mheap *mh, void (*print_heap_elem)(struct heap_elem*)) {
	printf("= mh:\n");
	for (int i = 0; i < mh->nheap; i++) {
		struct heap *h = mh->h[i];
		lock_acquire(&h->lk);
		printf("  Heap %d size %d last_vt %lld: \n", i, h->heap_size, h->last_vt);
		heap_iter(mh->h[i], print_heap_elem);
		lock_release(&h->lk);
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
		mycore()->nrand++;
		*j = c_rand(mh->nheap);
	}
}

static void __attribute__ ((noinline)) mh_rand_heap(struct mheap *mh, int i, int *j) {
	*j = c_rand(mh->nheap);
	while (i == *j) {
		mycore()->nrand++;
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
	mycore()->nretry_ins += r;
	return h;
}

// caller must hold heap lock
static struct heap_elem *mh_remove_min(struct heap *h) {
	struct heap_elem *he = heap_remove_min(h);
	assert(h->heap_size > 0);  // dummy should stay on heap
	return he;
}

static struct heap  __attribute__ ((noinline)) *mh_select(struct mheap *mh, int i, int j, vt_t *vt, vt_t *other_vt) {
	vt_t ovt;
	struct heap *h_i = mh->h[i];
	struct heap *h_j = mh->h[j];
	struct heap_elem *he_i = atomic_load_explicit(&h_i->heap[0],  __ATOMIC_RELAXED);
	struct heap_elem *he_j = atomic_load_explicit(&h_j->heap[0],  __ATOMIC_RELAXED);
	vt_t vt_i = atomic_load_explicit(&he_i->vruntime, __ATOMIC_RELAXED);
	vt_t vt_j = atomic_load_explicit(&he_j->vruntime, __ATOMIC_RELAXED);
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

// del min proc from h; may fail because some other core grabbed the min element
static struct heap_elem  __attribute__ ((noinline)) *mh_try_del_min(struct heap *h, vt_t vt) {
	int l = lock_try_acquire(&h->lk);
	if (l != 0) {
		return NULL;
	}
	vt_t vt0 = h->heap[0]->vruntime;
	// vt_t vt0 = h->min_vt;
	if (vt != vt0) {
		lock_release(&h->lk);
		return NULL;
	}
	struct heap_elem *he = mh_remove_min(h);
	// XXX p->tsc = safe_read_tsc();
	return he;
}

// if to_add is lower than min of heap, use to_add instead of min
static bool is_to_add_min(vt_t vt0, int w, struct heap_elem *to_add) {
	if (to_add == NULL)
		return false;
	if (vt0 == DUMMY)
		return true;
	if (vt0 < to_add->vruntime)
		return false;
	if ((vt0 == to_add->vruntime) && (w > to_add->weight))
		return false;
	return true;
}

// caller must have h locked
static struct heap_elem *mh_deq_min_or_use_to_add(struct heap *h, vt_t vt, int w, struct heap_elem *to_add) {
	struct heap_elem *he = NULL;
	if (is_to_add_min(vt, h->heap[0]->weight, to_add)) {
		// pretend we added and removed to_add from the heap
		h->last_vt = to_add->vruntime;
		he = to_add;
	} else if (vt != DUMMY) { 
		he = mh_remove_min(h);
		assert(he != NULL);
		if (to_add != NULL)  {
			mycore()->ndelay_yield++;
			heap_push(h, to_add);
		}
	}
	return he;
}

static struct heap_elem  __attribute__ ((noinline)) *mh_try_deq_min_enq(struct heap *h, vt_t vt, struct heap_elem *to_add) {
	int l = lock_try_acquire(&h->lk);
	if (l != 0) {
		return NULL;
	}
	vt_t vt0 = h->heap[0]->vruntime;
	// vt_t vt0 = h->min_vt;
	if (vt != vt0) {
		lock_release(&h->lk);
		return NULL;
	}
	struct heap_elem *he = mh_deq_min_or_use_to_add(h, vt, h->heap[0]->weight, to_add);
	lock_release(&h->lk);
	return he;
}

static struct heap_elem  __attribute__ ((noinline)) *mh_hint_min_proc(struct mheap *mh, struct heap *h) {
	struct heap_elem *he = NULL;
	lock_acquire(&h->lk);
	if (h->heap[0]->vruntime != DUMMY) {
		mycore()->npreempt_retry++;   // XXX fix; don't reuse name
		he = mh_remove_min(h);
	}
	lock_release(&h->lk);
	return he;
}

static struct heap_elem  __attribute__ ((noinline)) *mh_deq_min_enq(struct mheap *mh, struct heap_elem *to_add, struct heap *hint) {
	struct heap_elem *he;
	struct heap *h;
	long r = 0;
	int i, j;
	vt_t vt;
	vt_t other_vt;

	while(true) {
		mh_rand_heaps(mh, &i, &j);
		if ((h = mh_select(mh, i, j, &vt, &other_vt)) == NULL) {
			if(hint != NULL) {
				he = mh_hint_min_proc(mh, hint);
			}
			break;
		} 
		if ((he = mh_try_deq_min_enq(h, vt, to_add)) != NULL) {
			break;
		}
		r++;
	}

	mycore()->nretry_del += r;
	if(r > mycore()->max_retry_del)
		mycore()->max_retry_del = r;

	return he;
}

static struct heap_elem *mh_deq_min_one_heap(struct mheap *mh, struct heap_elem *to_add) {
	struct heap *h = mh->h[0];

	lock_acquire(&h->lk);
	struct heap_elem *he = mh_min(h);
	he = mh_deq_min_or_use_to_add(h, he->vruntime, he->weight, to_add);
	lock_release(&h->lk);
	return he;
}

struct heap_elem *mh_deq_min_elem(struct mheap *mh, struct heap *h) {
	if (mh->nheap == 1) {
		return mh_deq_min_one_heap(mh, NULL);
	}
	return mh_deq_min_enq(mh, NULL, h);
}

// if there is a min, grab it and enqueue to_add
struct heap_elem *mh_deq_min_elem_enq(struct mheap *mh, struct heap_elem *to_add, struct heap *hint) {
	if (mh->nheap == 1) {
		return mh_deq_min_one_heap(mh, to_add);
	}
	return mh_deq_min_enq(mh, to_add, hint);
}

// returns chosen h for e, so that caller can pass it to mh_remove_elem
struct heap *mh_insert_elem(struct mheap *mh, struct heap_elem *e) {
	struct heap *h = mh_choose_heap(mh);
	heap_push(h, e);
	lock_release(&h->lk);
	return h;
}

void mh_remove_elem(struct heap *h, struct heap_elem *e) {
	lock_acquire(&h->lk);
	heap_erase(h, e);
	lock_release(&h->lk);
}
