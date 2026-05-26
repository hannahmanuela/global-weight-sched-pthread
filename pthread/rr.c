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
// approximate round robin with multiheap with 1 or two groups (i.e., 2 priority levels)
//

extern bool debug;
extern bool do_preempt;
extern int num_groups;
extern struct sched_state *ss_global;

static void enqueue(struct task_struct *p) {
	if(debug) {
		struct core *c = get_mycore();
		printf("%d: enqueue_rr %d(%d) %p\n", c->cid, p->pid, p->group->gid, p->group->mh);
		//mh_print(p->group->mh);
	}
	struct heap *h = mh_choose_heap(p->group->mh);
	p->he.vruntime = safe_read_tsc();
	mh_add_process(p, h);
	lock_release(&h->lk);
}

static struct task_struct *ss_schedule_mh_enq(struct mheap *mh, struct task_struct *prev, bool all) {
	struct core *c = get_mycore();
	bool deq = (prev != NULL) && prev->mh == mh;
	assert(!prev || prev->mh != NULL);
	struct task_struct *p = mh_min_proc_enq(mh, deq ? prev : NULL, all);
	if(p != NULL) {
		assert(p->mh == mh);
		if(debug) {
			printf("%d: ss_schedule_mh_enq: %d(%d) vt %lld %p deq %d\n", c->cid, p->pid, p->group->gid, p->he.vruntime, mh, deq);
		}
		if (do_preempt && (prev != NULL) && !deq) {
			assert(prev->group->gid == RR_LOW);
			enqueue(prev);
		}
	}
	return p;
}

// Yield prev, if any, and select new one, if there is a runnable one
struct task_struct *ss_schedule_rr(struct task_struct *prev) {
	struct task_struct *p;
	bool low = false;
	bool preempted = atomic_load(&get_mycore()->preempted);

	if (do_preempt && preempted) {
		get_mycore()->npreempted += 1;
		atomic_store(&get_mycore()->preempted, false);
	}

	if(prev != NULL) {
		prev->he.vruntime = safe_read_tsc();
		low = (prev->group->gid == RR_LOW);
		if (debug)
			printf("%d: ss_schedule_rr: low %d prempted %d curp %d(%d)\n", get_mycore()->cid, low, preempted, prev->pid, prev->group->gid);
	} else {
		if (debug)
			printf("%d: ss_schedule_rr: low %d preempted %d idle\n", get_mycore()->cid, low, preempted);
	}


	// try high priority mh first for runnable proc
	if ((p = ss_schedule_mh_enq(ss_global->mh, prev, false)) != NULL) {
		assert(p->group->gid == RR_HIGH);
		goto ok;
	}

	// keep running high proc, if were running one
	if (prev != NULL && prev->group->gid == RR_HIGH) {
		if (debug) {
			printf("%d: ss_schedule_rr: locally run high %d(%d)\n", get_mycore()->cid, prev->pid, prev->group->gid);
		}
		p = prev;
		get_mycore()->nlocal += 1;
		goto ok;
	}

	if (num_groups > 1) {
		// check all high heaps for runnable proc if we were runnining
		// high (and didn't sample a new high) or we were running low
		// and were preempted
		bool check = !low || (low && preempted);
		if (check && (p = ss_schedule_mh_enq(ss_global->mh, prev, true)) != NULL) {
			assert(p->group->gid == RR_HIGH);
			goto ok;
		} else {
			get_mycore()->nrr_skip_high++;
		}

		// no proc in high heaps; go for low
		if ((p = ss_schedule_mh_enq(ss_global->mh1, prev, false)) != NULL) {
			assert(p->group->gid == RR_LOW);
			goto ok;
		}

		// keep running low proc, if were running one
		if (prev != NULL) {
			assert(prev->group->gid == RR_LOW);
			if (debug) {
				printf("%d: locally run low %d(%d) %p\n", get_mycore()->cid, prev->pid, prev->group->gid, ss_global->mh1);
			}
			get_mycore()->nlocal += 1;
			p = prev;
			goto ok;
		}
	}
	get_mycore()->nsched_null += 1;
	return NULL;

ok:
	if(debug) {
		printf("%d: running %d(%d)\n", get_mycore()->cid, p->pid, p->group->gid);
	}
	if (do_preempt && (p->group->gid == RR_LOW)) {
		// reset preemtable if switching from high to
		// a low proc, or if were prempted
		if(!low || preempted)
			preemptable_set(ss_global->preemptable, get_mycore()->cid);
	}
		
	c_lat(p);
	if(get_mycore()->fd > 0) {
		c_log_append(p);
	}
	return p;
}

// p wokeup: enqueue p at the ends of its group's queue
void ss_enqueue_rr(struct task_struct *p) {
	struct core *c = get_mycore();
	int cid = -1;
	assert(p->mh != NULL);
	if (do_preempt && p->group->gid == RR_HIGH) {
		cid = preemptable_find_and_clear(ss_global->preemptable);
	}
	if (debug) {
		printf("%d: ss_enqueue_rr %d(%d) dopreempt? %d\n", c->cid, p->pid, p->group->gid, cid);
	}
	if (cid != -1) {
		atomic_store(&ss_global->cs[cid]->preempted, true);
	}
	enqueue(p);
}

// p yields after it ran for a tick, do nothing until ss_schedule()
void ss_yield_rr(struct task_struct *p, t_t time_passed) {
	p->runtime += time_passed;
}

// process p goes to sleep
void ss_dequeue_rr(struct task_struct *p, t_t time_passed) {
	p->runtime += time_passed;
	if(debug) {
		printf("%d: %d(%d): dequeue %ld\n", get_mycore()->cid, p->pid, p->group->gid, time_passed);
		//mh_print(p->group->mh);
	}
}
