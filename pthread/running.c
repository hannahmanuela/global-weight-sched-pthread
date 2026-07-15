#include <stdio.h>
#include <stdatomic.h>
#include <assert.h>

#include "heap_elem.h"
#include "running.h"
#include "core.h"

//
// maintain a queue of running processes using mheap
//

void running_enq(struct mheap *mh, struct task_struct *p, int cid) {
	if (p->cid != NOCID) {
		printf("%d: running_set pid %d cid %d\n", mycore()->cid, p->pid, p->cid);
		assert(p->cid == NOCID);
	}
	atomic_store(&p->cid, cid);
	atomic_store(&p->he_r.vruntime, safe_read_tsc());
	p->h_r = mh_insert_elem(mh, &p->he_r);
	mycore()->npreempt_set++;
}

void running_rm(struct mheap *mh, struct task_struct *p) {
	assert(p->cid >= 0);
	mh_remove_elem(mh, p->h_r, &p->he_r);
	atomic_store(&p->cid, NOCID);
	mycore()->npreempt_clear++;
}

// return cid of core running a proc, sample enough heaps to get a low
// miss probability
int running_find_cid_deq(struct mheap *mh) {
	struct heap_elem *he = mh_deq_min_elem_sample(mh, running_nsample(mh));
	if(he != NULL) {
		mycore()->npreempt_find_ok++;
		struct task_struct *p = container_of(he, struct task_struct, he_r);
		int cid = atomic_load(&p->cid);
		if (cid == NOCID) {
			mycore()->npreempt_retry++;
		}
		return cid;
	}
	mycore()->npreempt_find_fail++;
	return NOCID;
}

// Exhaustive version of the find_find_cid_deq: scan every heap for a core running a low.
// Used as the fallback when the sampled find misses, so a runnable high still
// preempts a low-running core if one exists.
int running_find_cid_deq_all(struct mheap *mh) {
	struct heap_elem *he = mh_deq_min_elem_all_heap(mh, is_min_elem_vt, mycore()->cid);
	if(he != NULL) {
		mycore()->npreempt_find_ok++;
		struct task_struct *p = container_of(he, struct task_struct, he_r);
		int cid = atomic_load(&p->cid);
		if (cid == NOCID) {
			mycore()->npreempt_retry++;
		}
		return cid;
	}
	mycore()->npreempt_find_fail++;
	return NOCID;
}

