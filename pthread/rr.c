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
// approximate round robin with multiheap with 1 or two groups (i.e., priority levels)
//

extern bool debug;
extern int num_groups;

// Select next process to run from mh
static struct process *gh_schedule_mh(struct mheap *mh, struct core *c, bool all) {
	struct process *min_proc = NULL;
	min_proc = mh_min_proc(mh, c, all);
	if (min_proc == NULL) {
		return NULL;
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
		if(debug) {
			printf("%d: enq_proc %d(%d) %p\n", c->cid, c->process->pid, c->process->group->gid, mh);
		}
		struct heap *h = mh_choose_heap(mh, c);
		enq_proc_vt(gh, c, c->process, h);
	}
}

static struct process *gh_schedule_mh_enq(struct global_heap *gh, struct mheap *mh, struct core *c, bool all) {
	struct process *p = gh_schedule_mh(mh, c, all);
	if(p != NULL) {
		if(debug) {
			printf("%d: gh_schedule_mh_enq: %d(%d) vt %lld h %d\n", c->cid, p->pid, p->group->gid, p->he.vruntime, p->h->id);
		}
		enq_proc(gh, c);
		c->process = p;
		return p;
	}
	return NULL;
}

// Yield c->process, if any, and select new one, if there is a runnable one
bool gh_schedule_rr(struct global_heap *gh, struct core *c) {
	struct process *p;

	// try high priority mh first for runnable proc
	if ((p = gh_schedule_mh_enq(gh, gh->mh, c, false)) != NULL)
		goto ok; 

	// keep running high proc, if were running one
	if (c->process != NULL && c->process->group->gid == RR_HIGH) {
		if (debug) {
			printf("%d: gh_schedule_rr: locally run high %d\n", c->cid, c->process->pid);
		}
		c->nlocal += 1;
		goto ok;
	}

	if (num_groups > 1) {

		// check all high heaps for runnable proc
		if ((p = gh_schedule_mh_enq(gh, gh->mh, c, true)) != NULL) 
			goto ok;
		
		// no proc in high heaps; go for low
		if ((p = gh_schedule_mh_enq(gh, gh->mh1, c, false)) != NULL) 
			goto ok;

		// keep running low proc, if were running one
		if (c->process != NULL) {
			if (debug) {
				printf("%d: locally run low %d(%d) %p\n", c->cid, c->process->pid, c->process->group->gid, gh->mh1);
			}
			c->nlocal += 1;
			goto ok;
		}
	}
	c->nsched_null += 1;
	return false;

ok:
	if(c->fd > 0) {
		c_log_append(c, c->process);
	}
	return true;
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


