#include <stdio.h>
#include <assert.h>

#include "core.h"
#include "global_heap.h"
#include "mheap.h"
#include "pcrq.h"

extern bool debug;

bool gh_schedule_pcrq(struct global_heap *gh, struct core *c) {
	struct heap_elem *he = heap_remove_min(gh->mh->h[c->cid]);
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
	assert(p->h == gh->mh->h[c->cid]);
	heap_push(p->h, &p->he);
	if(debug) {
		printf("%d(%d): yield_pcrq %d\n", p->pid, p->group->gid, c->cid);
	}
}

void gh_enqueue_pcrq(struct global_heap *gh, struct core *c, struct process *p) {
	int i, j;
	mh_rand_heaps(gh->mh, c, &i, &j);
	if (gh->mh->h[i]->heap_size > gh->mh->h[j]->heap_size)
		i = j;
	p->h = gh->mh->h[i];
	p->he.vruntime = safe_read_tsc();
	heap_push(p->h, &p->he);
	if(debug) {
		printf("%d(%d): enqueue_pcrq %d\n", p->pid, p->group->gid, c->cid);
	}
}

void gh_dequeue_pcrq(struct global_heap *gh, struct core *c, struct process *p, t_t time_gotten) {
}
