#include <stdio.h>
#include <assert.h>

#include "core.h"
#include "global_heap.h"
#include "pcrq.h"

extern bool debug;

bool gh_schedule_pcrq(struct global_heap *gh, struct core *c) {
	struct heap_elem *he = heap_remove_min(c->runq);
	if(he == NULL)
		return false;
	struct process *p = (struct process *) he->elem;
	if(debug) {
		printf("%d: schedule %d(%d)\n", c->cid, p->pid, p->group->gid);
	}
	c->process = p;
	return true;
}

void gh_yield_pcrq(struct global_heap *gh, struct core *c, struct process *p, t_t time_passed) {
	p->runtime += time_passed;
	p->he.vruntime = safe_read_tsc();
	assert(p->h == c->runq);
	heap_push(c->runq, &p->he);
	if(debug) {
		printf("%d(%d): yield_pcrq %d\n", p->pid, p->group->gid, c->cid);
	}
}

void gh_enqueue_pcrq(struct global_heap *gh, struct core *mycore, struct process *p) {
	struct core *c = gh_choose_core(gh, mycore);
	heap_push(c->runq, &p->he);
	p->h = c->runq;
	p->he.vruntime = safe_read_tsc();
	if(debug) {
		printf("%d(%d): enqueue_pcrq %d\n", p->pid, p->group->gid, c->cid);
	}
}

void gh_dequeue_pcrq(struct global_heap *gh, struct core *c, struct process *p, t_t time_gotten) {
}
