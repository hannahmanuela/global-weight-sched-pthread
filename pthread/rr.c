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
#include "preempt.h"
#include "mheap.h"
#include "rr.h"

//
// approximate round robin with multiheap with 1 or two groups (i.e., priority levels)
//

extern bool debug;
extern bool do_preempt;
extern int num_groups;

static void enqueue(struct sched_state *ss, struct core *c, struct process *p) {
	if(debug) {
		printf("%d: enqueue_rr %d(%d) %p\n", c->cid, p->pid, p->group->gid, p->group->mh);
		//mh_print(p->group->mh);
	}
	struct heap *h = mh_choose_heap(p->group->mh, c);
	assert(p->h == NULL);
	p->he.vruntime = safe_read_tsc();
	mh_add_process(c, p, h);	
}

static struct process *ss_schedule_mh_enq(struct sched_state *ss, struct mheap *mh, struct core *c, bool all) {
	bool deq = (c->process != NULL) && c->process->mh == mh;
	assert(!c->process || c->process->mh != NULL);
	struct process *p = mh_min_proc_enq(mh, c, deq ? c->process : NULL, all);
	if(p != NULL) {
		assert(p->mh == mh);
		if(debug) {
			printf("%d: ss_schedule_mh_enq: %d(%d) vt %lld %p deq %d\n", c->cid, p->pid, p->group->gid, p->he.vruntime, mh, deq);
		}
		if (do_preempt && (c->process != NULL) && !deq) {
			assert(c->process->group->gid == RR_LOW);
			enqueue(ss, c, c->process);
		}
		c->process = p;
	} 
	return p;
}

// Yield c->process, if any, and select new one, if there is a runnable one
bool ss_schedule_rr(struct sched_state *ss, struct core *c) {
	struct process *p;
	bool low = false;
	bool preempted = atomic_load(&c->preempted);

	if (do_preempt && preempted) {
		c->npreempted += 1;
		atomic_store(&c->preempted, false);
	}

	if(c->process != NULL) {
		c->process->he.vruntime = safe_read_tsc();
		low = (c->process->group->gid == RR_LOW);
		if (debug)
			printf("%d: ss_schedule_rr: low %d prempted %d curp %d(%d)\n", c->cid, low, preempted, c->process->pid, c->process->group->gid);
	} else {
		if (debug)
			printf("%d: ss_schedule_rr: low %d preempted %d idle\n", c->cid, low, preempted);
	}


	// try high priority mh first for runnable proc
	if ((p = ss_schedule_mh_enq(ss, ss->mh, c, false)) != NULL) {
		assert(p->group->gid == RR_HIGH);
		goto ok; 
	}

	// keep running high proc, if were running one
	if (c->process != NULL && c->process->group->gid == RR_HIGH) {
		if (debug) {
			printf("%d: ss_schedule_rr: locally run high %d(%d)\n", c->cid, c->process->pid, c->process->group->gid);
		}
		c->nlocal += 1;
		goto ok;
	}

	if (num_groups > 1) {
		// check all high heaps for runnable proc if we were runnining
		// high (and didn't sample a new high) or we were running low
		// and were preempted
		bool check = !low || (low && preempted);
		if (check && (p = ss_schedule_mh_enq(ss, ss->mh, c, true)) != NULL) { 
			assert(p->group->gid == RR_HIGH);
			goto ok;
		} else {
			c->nrr_skip_high++;
		}
		
		// no proc in high heaps; go for low
		if ((p = ss_schedule_mh_enq(ss, ss->mh1, c, false)) != NULL) {
			assert(p->group->gid == RR_LOW);
			goto ok;
		}

		// keep running low proc, if were running one
		if (c->process != NULL) {
			assert(c->process->group->gid == RR_LOW);
			if (debug) {
				printf("%d: locally run low %d(%d) %p\n", c->cid, c->process->pid, c->process->group->gid, ss->mh1);
			}
			c->nlocal += 1;
			goto ok;
		}
	}
	c->nsched_null += 1;
	return false;

ok:
	if(debug) {
		printf("%d: running %d(%d)\n", c->cid, c->process->pid, c->process->group->gid);
	}
	if(c->process->h != NULL) {
		printf("%d: c->process->h %p\n", c->cid, c->process->h);
		assert(0);
	}
	assert(c->process->mh != NULL);
	if (do_preempt && (c->process->group->gid == RR_LOW)) {
		// reset preemtable if switching from high to
		// a low proc, or if were prempted
		if(!low || preempted) 
			preemptable_set(ss->preemptable, c->cid, c);
	}
		
	c_lat(c, p);
	if(c->fd > 0) {
		c_log_append(c, c->process);
	}
	return true;
}

// p wokeup: enqueue p at the ends of its group's queue
void ss_enqueue_rr(struct sched_state *ss, struct core *c, struct process *p) {
	int cid = -1;
	assert(p->mh != NULL);
	if (do_preempt && p->group->gid == RR_HIGH) {
		cid = preemptable_find_and_clear(ss->preemptable, c);
	}
	if (debug) {
		printf("%d: ss_enqueue_rr %d(%d) dopreempt? %d\n", c->cid, p->pid, p->group->gid, cid);
	}
	if (cid != -1) {
		atomic_store(&ss->cs[cid]->preempted, true);
	}
	enqueue(ss, c, p);
}

// p yields after it ran for a tick, do nothing until ss_schedule()
void ss_yield_rr(struct sched_state *ss, struct core *c, struct process *p, t_t time_passed) {
	p->runtime += time_passed;
}

// process p goes to sleep
void ss_dequeue_rr(struct sched_state *ss, struct core *c, struct process *p, t_t time_passed) {
	assert(c->process == p);
	p->runtime += time_passed;
	if(debug) {
		printf("%d: %d(%d): dequeue %ld\n", c->cid, p->pid, p->group->gid, time_passed);
		//mh_print(p->group->mh);
	}
	assert(p->h == NULL);
	c->process = NULL;
}


