#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>

#include "vt.h"
#include "util.h"
#include "driver.h"
#include "global_heap.h"
#include "core.h"
#include "mheap.h"
#include "rr.h"

//
// rr with multiheap with 1 or two groups (i.e., priority levels)
//

extern int debug;
extern bool use_localq;
extern int num_groups;

// Select next process to run from mh
struct process *gh_schedule_mh(struct mheap *mh, struct core *c, bool all) {
	struct process *min_proc = NULL;
	min_proc = mh_min_proc(mh, c, all);
	if (min_proc == NULL) {
		return NULL;
	}
	if(debug) {
		printf("%d: schedule_rr %d(%d) vt %lld h %d\n", c->cid, min_proc->pid, min_proc->group->gid, min_proc->he.vruntime, min_proc->h->id);
		// mh_print(min_proc->mh);
	}
	if(c->fd > 0) {
		c_log_append(c, min_proc);
	}
	return min_proc;
}

static void enq_proc_vt(struct global_heap *gh, struct core *c, struct process *p, struct heap *h) {
	p->he.vruntime = safe_read_tsc();
	mh_add_process(c, p, h);
}

static void enq_proc(struct global_heap *gh, struct core *c) {
	if(c->process != NULL) {
		struct mheap *mh = (c->process->group->gid == RR_HIGH) ? gh->mh : gh->mh1;
		struct heap *h = mh_choose_heap(gh->mh, c);
		enq_proc_vt(gh, c, c->process, h);
	}
}

// Select next process to run
bool gh_schedule_rr(struct global_heap *gh, struct core *c) {
	struct process *p;
	p = gh_schedule_mh(gh->mh, c, false);
	if(p != NULL) {
		enq_proc(gh, c);
		c->process = p;
		return true;
	}
	if (c->process != NULL && c->process->group->gid == RR_HIGH) {
		c->nlocal += 1;
		return true;
	}
	if (num_groups > 1) {
		p = mh_min_proc(gh->mh, c, true);
		if(p  != NULL) {
			enq_proc(gh, c);
			c->process = p;
			return true;
		}
		// nothing in high heap; go for low
		p = gh_schedule_mh(gh->mh1, c, false);
		if(p != NULL) {
			enq_proc(gh, c);
			c->process = p;
			return true;
		}
		if (c->process != NULL) {
			c->nlocal += 1;
			return true;
		}
		if (debug) {
			printf("%d: run low %d(%d) %p\n", c->cid, p->pid, p->group->gid, gh->mh1);
		}
	}
	return false;
}


// p wokeup: enqueue p at the ends of its group's queue
void gh_enqueue_rr(struct global_heap *gh, struct core *c, struct process *p) {
	struct heap *h = mh_choose_heap(p->group->mh, c);
	assert(p->h == NULL);

	enq_proc_vt(gh, c, p, h);

	if(debug) {
		printf("%d(%d): enqueue_rr %p\n", p->pid, p->group->gid, p->group->mh);
		//mh_print(p->group->mh);
	}
}

// p yields after it ran for a tick, do nothing until gh_schedule()
void gh_yield_rr(struct global_heap *gh, struct core *c, struct process *p, t_t time_passed) {
	p->runtime += time_passed;
}

// process p goes to sleep
void gh_dequeue_rr(struct global_heap *gh, struct core *c, struct process *p, t_t time_passed) {
	p->runtime += time_passed;
	if(debug) {
		printf("%d(%d): dequeue %ld\n", p->pid, p->group->gid, time_passed);
		//mh_print(p->group->mh);
	}
	p->h = NULL;
	c->process = NULL;
}


