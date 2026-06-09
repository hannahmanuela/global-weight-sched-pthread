#include <stdio.h>
#include <stdatomic.h>

#include "running.h"
#include "core.h"

void running_set(struct mheap *mh, struct task_struct *p, int cid) {
	p->cid = cid;
	p->he.vruntime = safe_read_tsc();
	mh_insert_proc(mh, p);
	mycore()->npreempt_set++;
}

bool running_clear(struct mheap *mh, struct task_struct *p) {
	p->cid = -1;
	mycore()->npreempt_clear++;
	mh_remove_proc(mh, p);
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

