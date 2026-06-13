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
#include "running.h"
#include "mheap.h"
#include "rr.h"

//
// approximate round robin with multiheap with 1 or two groups (i.e., 2 priority levels)
//

extern bool debug;
extern bool do_preempt;
extern int num_groups;
extern bool use_runningq;
extern struct sched_state *ss_global;

static void enqueue(struct task_struct *p) {
	if(debug) {
		struct core *c = mycore();
		printf("%d: enqueue_rr %d(%d) %p\n", c->cid, p->pid, p->group->gid, p->group->mh);
	}
	p->he.vruntime = safe_read_tsc();
	p->h = mh_insert_elem(p->group->mh, &p->he);
}

static struct task_struct *ss_schedule_mh_enq(struct mheap *mh, struct task_struct *prev, bool all) {
	struct core *c = mycore();
	bool deq = (prev != NULL) && (prev->group->mh == mh);
	struct task_struct *p = mh_min_proc_enq(mh, deq ? prev : NULL, all);
	if(p != NULL) {
		assert(p->group->mh == mh);
		if(debug) {
			printf("%d: ss_schedule_mh_enq: %d(%d) vt %lld %p deq %d\n", c->cid, p->pid, p->group->gid, p->he.vruntime, mh, deq);
		}
		if (do_preempt && (prev != NULL) && !deq) {
			assert(prev->group->gid == RR_LOW);
			if (use_runningq) {
				if (debug) {
					printf("%d: %d(%d) remove from runq %d\n", mycore()->cid, prev->pid, prev->group->gid, prev->cid);
				}
				running_clear(ss_global->mh_r, prev);
			}
			enqueue(prev);
		}
	}
	return p;
}

// Yield prev, if any, and select new one, if there is a runnable one
struct task_struct *ss_schedule_rr(struct task_struct *prev) {
	struct task_struct *p;
	bool low = false;
	bool preempted = atomic_load(&mycore()->preempted);

	if (do_preempt && preempted) {
		mycore()->npreempted += 1;
		atomic_store(&mycore()->preempted, false);
	}

	if(prev != NULL) {
		prev->he.vruntime = safe_read_tsc();
		low = (prev->group->gid == RR_LOW);
		if (debug)
			printf("%d: ss_schedule_rr: low %d prempted %d curp %d(%d)\n", mycore()->cid, low, preempted, prev->pid, prev->group->gid);
	} else {
		if (debug)
			printf("%d: ss_schedule_rr: low %d preempted %d idle\n", mycore()->cid, low, preempted);
	}


	// try high priority mh first for runnable proc
	if ((p = ss_schedule_mh_enq(ss_global->mh, prev, false)) != NULL) {
		assert(p->group->gid == RR_HIGH);
		goto ok;
	}

	// keep running high proc, if were running one
	if (prev != NULL && prev->group->gid == RR_HIGH) {
		if (debug) {
			printf("%d: ss_schedule_rr: locally run high %d(%d)\n", mycore()->cid, prev->pid, prev->group->gid);
		}
		p = prev;
		mycore()->nlocal += 1;
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
			mycore()->nrr_skip_high++;
		}

		// no proc in high heaps; go for low
		if ((p = ss_schedule_mh_enq(ss_global->mh_l, prev, false)) != NULL) {
			assert(p->group->gid == RR_LOW);
			goto ok;
		}

		// keep running low proc, if were running one
		if (prev != NULL) {
			assert(prev->group->gid == RR_LOW);
			if (debug) {
				printf("%d: locally run low %d(%d) %p\n", mycore()->cid, prev->pid, prev->group->gid, ss_global->mh_l);
			}
			mycore()->nlocal += 1;
			p = prev;
			goto ok;
		}
	}
	mycore()->nsched_null += 1;
	return NULL;

ok:
	if(debug) {
		printf("%d: running %d(%d)\n", mycore()->cid, p->pid, p->group->gid);
	}
	if (do_preempt && (p->group->gid == RR_LOW)) {
		if (use_runningq) {
			if (p->cid != -1) {
				if (debug)  {
					printf("%d: %d(%d) continue running cid %d\n", mycore()->cid,
				       p->pid, p->group->gid, p->cid);
				}
			} else {
				running_set(ss_global->mh_r, p, mycore()->cid);
			}
		} else {
			// reset preemtable if switching from high to
			// a low proc, or if were prempted
			if(!low || preempted)
				preemptable_set(ss_global->preemptable, mycore()->cid);
		}
	}
		
	c_lat(p);
	if(mycore()->fd > 0) {
		c_log_append(p);
	}
	return p;
}

// p wokeup: enqueue p at the ends of its group's queue
void ss_enqueue_rr(struct task_struct *p) {
	struct core *c = mycore();
	int cid = -1;
	if (do_preempt && p->group->gid == RR_HIGH) {
		if (use_runningq) {
			cid = running_find_and_clear(ss_global->mh_r);
		} else {
			cid = preemptable_find_and_clear(ss_global->preemptable);
		}
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
// XXX remove from preemtable and running
void ss_dequeue_rr(struct task_struct *p, t_t time_passed) {
	p->runtime += time_passed;
	if(debug) {
		printf("%d: %d(%d): dequeue %ld\n", mycore()->cid, p->pid, p->group->gid, time_passed);
		//mh_print(p->group->mh);
	}
}
