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

//
// global_heap with rr
//

extern int debug;

// Select next process to run from gid
struct process *gh_schedule_rr_gid(struct global_heap *gh, struct core *c, int gid) {
	struct process *min_proc = NULL;
	min_proc = mh_min_proc(gh->mh, c);
	if (min_proc == NULL) {
		c->process = NULL;
		return NULL;
	}

	if(debug) {
		printf("%d: schedule_rr %d(%d) vt %lld h %d\n", c->cid, min_proc->pid, min_proc->group->gid, min_proc->he.vruntime, min_proc->h->id);
		mh_print(min_proc->mh);
	}
	if(c->fd > 0) {
		c_log_append(c, min_proc);
	}
	c->process = min_proc;
	return min_proc;
}

// Select next process to run
struct process *gh_schedule_rr(struct global_heap *gh, struct core *c) {
	return gh_schedule_rr_gid(gh, c, 0);
}

static void enq_proc_vt(struct global_heap *gh, struct core *c, struct process *p, struct heap *h) {
	p->he.vruntime = safe_read_tsc();
	mh_add_process(c, p, h);
}

// Add p to group and make p runnable
void gh_enqueue_rr(struct global_heap *gh, struct core *c, struct process *p) {
	struct heap *h = mh_choose_heap(p->mh, c);
	assert(p->h == NULL);

	enq_proc_vt(gh, c, p, h);

	if(debug) {
		printf("%d(%d): enqueue_rr nthread %d lh %p vt %lld gvt %lld\n", p->pid, p->group->gid, p->group->nthread, p->h, p->he.vruntime, p->group->vruntime);
		mh_print(p->group->mh);
	}
}

// Yield and enqueue
void gh_yield_rr(struct global_heap *gh, struct core *c, struct process *p, t_t time_passed) {
	struct heap *h = mh_choose_heap(p->mh, c);

	enq_proc_vt(gh, c, p, h);

	if(debug) {
		printf("%d(%d): yield_rr time_passed %ld nt %d w %d vt %lld h %d\n", p->pid, p->group->gid, time_passed, p->group->nthread, p->he.weight, p->he.vruntime, h->id);
		mh_print(p->group->mh);
	}
}

// Process p is not runnable and yields core, which may make
// p's group not runnable
void gh_dequeue_rr(struct global_heap *gh, struct core *c, struct process *p, t_t time_passed) {
}

