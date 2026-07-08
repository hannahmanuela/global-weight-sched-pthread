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

int running_find_and_clear(struct mheap *mh) {
	struct heap_elem *he = mh_deq_min_elem(mh, -1);
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

