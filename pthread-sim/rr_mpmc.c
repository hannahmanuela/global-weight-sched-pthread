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
#include "mpmc.h"
#include "rr.h"

//
// rr with multiheap with 1 or two groups (i.e., priority levels)
//

extern int debug;

// Select next process to run from mh
struct process *gh_schedule_q(queue_t *q, struct core *c) {
	struct process *min_proc = NULL;
	min_proc = queue_pop(q);
	if (min_proc == NULL) {
		c->process = NULL;
		return NULL;
	}

	if(debug) {
		printf("%d: schedule_rr %d(%d) vt %lld\n", c->cid, min_proc->pid, min_proc->group->gid, min_proc->he.vruntime);
	}
	if(c->fd > 0) {
		c_log_append(c, min_proc);
	}
	c->process = min_proc;
	return min_proc;
}

// Select next process to run
struct process *gh_schedule_rr(struct global_heap *gh, struct core *c) {
	struct process *p;
	p = gh_schedule_q(&gh->q, c);
	if(p != NULL) {
		return p;
	}
	p = gh_schedule_q(&gh->q1, c);
	if(p == NULL) {
		return p;
	}
	if (debug) {
		printf("%d: run low %d(%d) %p\n", c->cid, p->pid, p->group->gid, &gh->q1);
	}
	return p;
}

static void enq_proc_vt(struct global_heap *gh, struct core *c, struct process *p) {
	p->he.vruntime = safe_read_tsc();
	if(p->group->gid == RR_HIGH) queue_push(&gh->q, p);
	else queue_push(&gh->q1, p);
}

// Enqueue p at the ends of its group's queue
void gh_enqueue_rr(struct global_heap *gh, struct core *c, struct process *p) {
	enq_proc_vt(gh, c, p);

	if(debug) {
		printf("%d(%d): enqueue_rr %p\n", p->pid, p->group->gid, p->group->mh);
	}
}

// Process p yields after it ran for a tick, append it to the end of its queue
void gh_yield_rr(struct global_heap *gh, struct core *c, struct process *p, t_t time_passed) {
	p->runtime += time_passed;
	enq_proc_vt(gh, c, p);

	if(debug) {
		printf("%d(%d): yield_rr time_passed %ld\n", p->pid, p->group->gid, time_passed);
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


