#include <stdio.h>
#include <stdatomic.h>
#include <assert.h>

#include "running.h"
#include "core.h"

//
// maintain a queue of running processes using mheap
//

void running_set(struct mheap *mh, struct task_struct *p, int cid) {
	assert(p->cid == -1);
	p->cid = cid;
	p->he_r.vruntime = safe_read_tsc();
	p->h_r = mh_insert_elem(mh, &p->he_r);
	mycore()->npreempt_set++;
}

bool running_clear(struct mheap *mh, struct task_struct *p) {
	assert(p->cid >= 0);
	p->cid = -1;
	mycore()->npreempt_clear++;
	mh_remove_elem(p->h_r, &p->he_r);
}

int running_find_and_clear(struct mheap *mh) {
	struct heap_elem *he = mh_deq_min_elem_enq(mh, NULL, false);
	if(he != NULL) {
		mycore()->npreempt_find_ok++;
		struct task_struct *p =  container_of(he, struct task_struct, he);
		return p->cid;
	}
	mycore()->npreempt_find_fail++;
	return -1;
}

