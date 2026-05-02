#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>

#include "vt.h"
#include "util.h"
#include "driver.h"
#include "sched_state.h"
#include "core.h"
#include "mpmc.h"
#include "rr.h"

//
// rr with global queue
//

extern bool debug;

// Select next process to run from mh
struct process *gh_schedule_q(queue_t *q, struct core *c) {
	struct process *min_proc = NULL;
	min_proc = queue_pop(q);
	if (min_proc == NULL) {
		c->process = NULL;
		c->nsched_null += 1;
		return NULL;
	}

	if(debug) {
		printf("%d: schedule_gq %d(%d) vt %lld\n", c->cid, min_proc->pid, min_proc->group->gid, min_proc->he.vruntime);
	}
	if(c->fd > 0) {
		c_log_append(c, min_proc);
	}
	c->process = min_proc;
	return min_proc;
}

// Select next process to run
struct process *gh_schedule_gq(struct global_heap *gh, struct core *c) {
	struct process *p;
	p = gh_schedule_q(&gh->q, c);
	return p;
}

static void enq_proc_vt(struct global_heap *gh, struct core *c, struct process *p) {
	p->he.vruntime = safe_read_tsc();
	queue_push(&gh->q, p);
}

// Enqueue p at the ends of its group's queue
void gh_enqueue_gq(struct global_heap *gh, struct core *c, struct process *p) {
	enq_proc_vt(gh, c, p);

	if(debug) {
		printf("%d(%d): enqueue_gq %p\n", p->pid, p->group->gid, p->group->mh);
	}
}

// Process p yields after it ran for a tick, append it to the end of its queue
void gh_yield_gq(struct global_heap *gh, struct core *c, struct process *p, t_t time_passed) {
	p->runtime += time_passed;
	enq_proc_vt(gh, c, p);

	if(debug) {
		printf("%d(%d): yield_gq time_passed %ld\n", p->pid, p->group->gid, time_passed);
	}
}

// Process p is not runnable and yields core
void gh_dequeue_gq(struct global_heap *gh, struct core *c, struct process *p, t_t time_passed) {
	p->runtime += time_passed;
	if(debug) {
		printf("%d(%d): dequeue_gq %ld\n", p->pid, p->group->gid, time_passed);
	}
	p->h = NULL;
}


