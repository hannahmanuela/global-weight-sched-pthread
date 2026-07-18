#ifndef _MHEAP_H_

#define _MHEAP_H_

#include "util.h"
#include "heap_elem.h"
#include "core.h"
#include "group.h"
#include "heap.h"

struct mheap {
	struct heap **h __calign__;
	int nheap;
	is_lt_elem_t lt;
};


// Number of heaps to sample in the find to achieve a given miss
// rate. For example, the running queue holds ~ncore low procs spread by
// power-of-two over nheap == 2*ncore heaps, so the empty fraction is
// p = (nheap - ncore + 1)/nheap ~= 1/2, roughly independent of core
// count. A sample of s heaps misses (finds nothing though a low is
// running) with prob ~= p^s, so pick the smallest s with p^s <=
// MH_R_MISS_EPS, capped at nheap.
#define MH_R_MISS_EPS 0.01

static int running_nsample(struct mheap *mh) {
	int nheap = mh->nheap;
	int ncore = nheap / 2;              // running queue is sized 2*ncore
	if (ncore < 1) ncore = 1;
	int nempty = nheap - ncore + 1;    // ~ncore+1 empty heaps
	double p = (double) nempty / (double) nheap;
	double miss = 1.0;
	int s = 0;
	while (miss > MH_R_MISS_EPS && s < nheap) {
		miss *= p;
		s++;
	}
	if (s < 2) s = 2;
	return s;
}

// Like running_nsample() but for sampling CPUS (cores) directly rather than the
// 2*ncore dispatch heaps -- e.g. the preemption-target search, which samples cores
// to find a running lower-priority (over-served) task to kick. The population is
// ncore cores, not 2*ncore heaps, so the cap is ncore. At the preemption boundary
// roughly half the cores are not valid targets (idle, or running a task >= the
// waker's priority), so model p ~ 1/2 as above: smallest s with p^s <= EPS.
static int nsamples_cores(int ncore) {
	if (ncore < 1) ncore = 1;
	double p = 0.5;
	double miss = 1.0;
	int s = 0;
	while (miss > MH_R_MISS_EPS && s < ncore) {
		miss *= p;
		s++;
	}
	if (s < 2) s = 2;
	if (s > ncore) s = ncore;
	return s;
}

struct mheap *mh_new(int n, is_lt_elem_t lt);
void mh_stats(struct mheap *mh);
void mh_free(struct mheap *mh);
void mh_print(struct mheap *mh, void (*print_heap_elem)(struct heap_elem *));
struct heap *mh_heap(struct mheap *mh, int i);
vt_t mh_min_vt(struct heap *h);
vt_t mh_last_vt(struct heap *h);
struct heap_elem *mh_deq_min_elem(struct mheap *mh, int hint);
struct heap_elem *mh_deq_min_elem_enq(struct mheap *mh, struct heap_elem *p, int hint);
struct heap_elem *mh_deq_min_elem_all_heap(struct mheap *mh, is_min_elem_t minf, int hint);
struct heap_elem *mh_deq_min_elem_sample(struct mheap *mh, int nsample);
struct heap *mh_choose_heap(struct mheap *mh);
float mh_load(struct mheap *mh, int *maxl);
void mh_rand_heaps(struct mheap *mh, int *i, int *j);
int mh_insert_elem(struct mheap *mh, struct heap_elem *e);
void mh_remove_elem(struct mheap *mh, int hi, struct heap_elem *e);

#endif
