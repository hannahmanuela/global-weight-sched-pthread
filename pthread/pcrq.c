#include <stdio.h>
#include <assert.h>

#include "core.h"
#include "sched_state.h"
#include "mheap.h"
#include "pcrq.h"

// per-core runqueue scheduler

extern bool debug;

bool ss_schedule_pcrq(struct sched_state *ss, struct core *c) {
	struct heap_elem *he = heap_remove_min(ss->mh->h[c->cid]);
	if(he == NULL)
		return false;
	struct task_struct *p = (struct task_struct *) he->elem;
	if(debug) {
		printf("%d: schedule %d(%d)\n", c->cid, p->pid, p->group->gid);
	}
	c->process = p;
	if(c->fd > 0) {
		c_log_append(c, p);
	}
	return true;
}

void ss_yield_pcrq(struct sched_state *ss, struct core *c, struct task_struct *p, t_t time_passed) {
	p->runtime += time_passed;
	p->he.vruntime = safe_read_tsc();
	assert(p->h == ss->mh->h[c->cid]);
	heap_push(p->h, &p->he);
	if(debug) {
		printf("%d(%d): yield_pcrq %d\n", p->pid, p->group->gid, c->cid);
	}
}

void ss_enqueue_pcrq(struct sched_state *ss, struct core *c, struct task_struct *p) {
	int i, j;
	mh_rand_heaps(ss->mh, &i, &j);
	if (ss->mh->h[i]->heap_size > ss->mh->h[j]->heap_size)
		i = j;
	p->h = ss->mh->h[i];
	p->he.vruntime = safe_read_tsc();
	heap_push(p->h, &p->he);
	if(debug) {
		printf("%d(%d): enqueue_pcrq %d\n", p->pid, p->group->gid, c->cid);
	}
}

void ss_dequeue_pcrq(struct sched_state *ss, struct core *c, struct task_struct *p, t_t time_gotten) {
}
