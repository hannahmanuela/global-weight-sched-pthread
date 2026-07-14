#include <stdio.h>
#include <stdatomic.h>
#include <assert.h>

#include "heap_elem.h"
#include "running.h"
#include "core.h"

//
// maintain a queue of running processes using mheap
//

void running_set(struct mheap *mh, struct task_struct *p, int cid) {
	if (p->cid != -1) {
		printf("%d: running_set pid %d cid %d\n", mycore()->cid, p->pid, p->cid);
		assert(p->cid == -1);
	}
	atomic_store(&p->cid, cid);
	atomic_store(&p->he_r.vruntime, safe_read_tsc());
	p->h_r = mh_insert_elem(mh, &p->he_r);
	mycore()->npreempt_set++;
}

bool running_clear(struct mheap *mh, struct task_struct *p) {
	assert(p->cid >= 0);
	mh_remove_elem(mh, p->h_r, &p->he_r);
	atomic_store(&p->cid, -1);
	mycore()->npreempt_clear++;
}

// Number of heaps to sample in the find. The running queue holds ~ncore low
// procs spread by power-of-two over nheap == 2*ncore heaps, so the empty
// fraction is p = (nheap - ncore + 1)/nheap ~= 1/2, roughly independent of core
// count. A sample of s heaps misses (finds nothing though a low is running) with
// prob ~= p^s, so pick the smallest s with p^s <= MH_R_MISS_EPS, capped at nheap.
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

int running_find_and_clear(struct mheap *mh) {
	struct heap_elem *he = mh_deq_min_elem_sample(mh, running_nsample(mh));
	if(he != NULL) {
		mycore()->npreempt_find_ok++;
		struct task_struct *p = container_of(he, struct task_struct, he_r);
		int cid = atomic_load(&p->cid);
		if (cid == -1) {
			mycore()->npreempt_retry++;
		}
		return cid;
	}
	mycore()->npreempt_find_fail++;
	return -1;
}

// Exhaustive version of the find: scan every heap for a core running a low.
// Used as the fallback when the sampled find misses, so a runnable high still
// preempts a low-running core if one exists -- regardless of which core enqueued
// it. (The old scan_high fallback only worked when the enqueuing core was itself
// running a low; a high enqueuing a high would set scan_high uselessly.)
int running_find_and_clear_all(struct mheap *mh) {
	struct heap_elem *he = mh_deq_min_elem_all_heap(mh, is_min_elem_vt, mycore()->cid);
	if(he != NULL) {
		mycore()->npreempt_find_ok++;
		struct task_struct *p = container_of(he, struct task_struct, he_r);
		int cid = atomic_load(&p->cid);
		if (cid == -1) {
			mycore()->npreempt_retry++;
		}
		return cid;
	}
	mycore()->npreempt_find_fail++;
	return -1;
}

