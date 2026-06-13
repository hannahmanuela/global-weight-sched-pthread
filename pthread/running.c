#include <stdio.h>
#include <stdatomic.h>
#include <assert.h>

#include "running.h"
#include "core.h"

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
	struct task_struct *p = mh_min_proc_enq(mh, NULL, false);
	if(p != NULL) {
		mycore()->npreempt_find_ok++;
		return p->cid;
	}
	mycore()->npreempt_find_fail++;
	return -1;
}

