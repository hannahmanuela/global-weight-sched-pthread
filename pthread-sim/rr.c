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

static bool gh_select(struct mheap *mh, struct core *c) {
}

// Select next process to run from mh
struct process *gh_schedule_mh(struct mheap *mh, struct core *c, bool all) {
	struct process *min_proc = NULL;
	min_proc = mh_min_proc(mh, c, all);
	if (min_proc == NULL) {
		c->process = NULL;
		return NULL;
	}

	if(debug) {
		printf("%d: schedule_rr %d(%d) vt %lld h %d\n", c->cid, min_proc->pid, min_proc->group->gid, min_proc->he.vruntime, min_proc->h->id);
		// mh_print(min_proc->mh);
	}
	if(c->fd > 0) {
		c_log_append(c, min_proc);
	}
	c->process = min_proc;
	return min_proc;
}

// Select next process to run
bool gh_schedule_rr(struct global_heap *gh, struct core *c) {
	struct process *p;
	if ((p = c->rqueue) != NULL) {
		c->rqueue = NULL;
		c->process = p;
		c->nlocal += 1;
		return true;
	}
	p = gh_schedule_mh(gh->mh, c, false);
	if(p != NULL) {
		mc_dec(gh->mc, c);
		return true;
	}
	if (num_groups > 1) {
		p = gh_schedule_mh(gh->mh1, c, false);
		if(p == NULL) {
			mc_dec(gh->mc1, c);
			return true;
		}
		if (debug) {
			printf("%d: run low %d(%d) %p\n", c->cid, p->pid, p->group->gid, gh->mh1);
		}
	}
	return false;
}

// enqueue locally if no processes in global run queues
static bool enq_local(struct global_heap *gh, struct core *c, struct process *p) {
	if(!use_localq)
		return false;
	bool high_empty = mc_is_zero(gh->mc, c);
	if((c->rqueue == NULL) && (p->group->gid == RR_HIGH) && high_empty) {
		c->rqueue = p;
		// printf("%d: enq: local %d/%d\n", c->cid, p->pid, p->group->gid);
		return true;
	}
	if ((num_groups > 1) && high_empty && (c->rqueue == NULL) && mc_is_zero(gh->mc1, c)) {
		c->rqueue = p;
		return true;
	}
	if(debug) printf("%d: enq: global pid %d/%d q %d empty %d\n", c->cid, p->pid, p->group->gid, c->rqueue != NULL, high_empty);
	return false;
}

static void enq_proc_vt(struct global_heap *gh, struct core *c, struct process *p, struct heap *h) {
	p->he.vruntime = safe_read_tsc();
	if(p->group->gid == RR_HIGH) mc_inc(gh->mc, c);
	else mc_inc(gh->mc1, c);
	mh_add_process(c, p, h);
}

// Enqueue p at the ends of its group's queue
void gh_enqueue_rr(struct global_heap *gh, struct core *c, struct process *p) {
	if (enq_local(gh, c, p))
		return;
	
	struct heap *h = mh_choose_heap(p->group->mh, c);
	assert(p->h == NULL);

	enq_proc_vt(gh, c, p, h);

	if(debug) {
		printf("%d(%d): enqueue_rr %p\n", p->pid, p->group->gid, p->group->mh);
		//mh_print(p->group->mh);
	}
}

// Process p yields after it ran for a tick, append it to the end of its queue
void gh_yield_rr(struct global_heap *gh, struct core *c, struct process *p, t_t time_passed) {
	p->runtime += time_passed;

	if (enq_local(gh, c, p))
		return;

	struct heap *h = mh_choose_heap(p->group->mh, c);

	enq_proc_vt(gh, c, p, h);

	if(debug) {
		printf("%d(%d): yield_rr time_passed %ld h %d\n", p->pid, p->group->gid, time_passed, h->id);
		//mh_print(p->group->mh);
	}
}

// Process p is not runnable and yields core
void gh_dequeue_rr(struct global_heap *gh, struct core *c, struct process *p, t_t time_passed) {
	p->runtime += time_passed;
	if(debug) {
		printf("%d(%d): dequeue %ld\n", p->pid, p->group->gid, time_passed);
		//mh_print(p->group->mh);
	}
	p->h = NULL;
}


