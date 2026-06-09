#include <stdio.h>
#include <assert.h>

#include "core.h"
#include "sched_state.h"
#include "mheap.h"
#include "pcrq.h"

// per-core runqueue scheduler

extern bool debug;
extern struct sched_state *ss_global;

struct task_struct *ss_schedule_pcrq(struct task_struct *prev) {
	struct core *c = mycore();
	if (prev != NULL) {
		prev->he.vruntime = safe_read_tsc();
		assert(prev->h == ss_global->mh->h[c->cid]);
		heap_push(prev->h, &prev->he);
		if(debug) {
			printf("%d(%d): yield_pcrq %d\n", prev->pid, prev->group->gid, c->cid);
		}
	}
	struct heap_elem *he = heap_remove_min(ss_global->mh->h[c->cid]);
	if(he == NULL)
		return NULL;
	struct task_struct *p = container_of(he, struct task_struct, he);
	if(debug) {
		printf("%d: schedule %d(%d)\n", c->cid, p->pid, p->group->gid);
	}
	if(c->fd > 0) {
		c_log_append(p);
	}
	return p;
}

void ss_yield_pcrq(struct task_struct *p, t_t time_passed) {
	p->runtime += time_passed;
}

void ss_enqueue_pcrq(struct task_struct *p) {
	struct core *c = mycore();
	int i, j;
	mh_rand_heaps(ss_global->mh, &i, &j);
	if (ss_global->mh->h[i]->heap_size > ss_global->mh->h[j]->heap_size)
		i = j;
	p->h = ss_global->mh->h[i];
	p->he.vruntime = safe_read_tsc();
	heap_push(p->h, &p->he);
	if(debug) {
		printf("%d(%d): enqueue_pcrq %d\n", p->pid, p->group->gid, c->cid);
	}
}

void ss_dequeue_pcrq(struct task_struct *p, t_t time_gotten) {
}
