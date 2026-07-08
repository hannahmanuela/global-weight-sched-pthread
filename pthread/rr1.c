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
#include "runnable.h"
#include "running.h"
#include "mheap.h"
#include "rr.h"

//
// approximate round robin with one or two priority levels using one mheap
//

extern bool debug;
extern bool do_preempt;
extern int num_groups;
extern bool use_runningq;
extern struct sched_state *ss_global;

static int enqueue(struct task_struct *p) {
	if(debug) {
		struct core *c = mycore();
		printf("%d: enqueue_rr1 %d(%d) at %lld\n", c->cid, p->pid, p->group->weight, p->he.vruntime);
	}
	atomic_store(&p->he.vruntime, tsc_now());
	int h = mh_insert_elem(p->group->mh, &p->he);
	return h;
}

// Yield prev, if any, and select new one, if there is a runnable one
struct task_struct *ss_schedule_rr1(struct task_struct *prev) {
	struct task_struct *p = NULL;
	struct task_struct *p_locked = NULL;
	bool low = false;
	int preempted = atomic_load(&mycore()->preempted);

	if (do_preempt && preempted != -1) {
		mycore()->npreempted += 1;
		atomic_store(&mycore()->preempted, -1);
	}

	if(prev != NULL) {
		atomic_store(&prev->he.vruntime, tsc_now());
		low = (prev->he.weight == W_LOW);
		if (debug)
			printf("%d: ss_schedule_rr1: low %d preempted heap %d prev %d(%d) scan %d\n", mycore()->cid, low, preempted, prev->pid, prev->he.weight, mycore()->scan_high);
	} else {
		if (debug)
			printf("%d: ss_schedule_rr1: preempted heap %d idle scan %d\n", mycore()->cid, preempted, mycore()->scan_high);
	}

	if (use_runningq && (prev != NULL) && (prev->he.weight == W_LOW)) {
		// lock prev because it might end up on runnable queue
		// and some core may grab it and add it to the running queue
		// while it is still on the running queue now.
		lock_acquire(&prev->lk);
		p_locked = prev;
	}

	if (mycore()->scan_high) { 
		if (debug) {
			printf("%d: scan high prev %p\n", mycore()->cid, prev);
		}
		mycore()->scan_high = false;

		// XXX doesn't happen with -r 2 -b 2 but should fix.
		assert(prev == NULL);

		mycore()->nscan_all++;
		// we dequeued a high priority process; do our best to find a new one
		if ((p = runnable_deq_high_proc_all_heap(ss_global->mh)) != NULL) {
			mycore()->nscan_all_ok++;
			goto ok;
		}
	}

	// find a proc to run
	if ((p = runnable_deq_proc_hint(ss_global->mh, prev, preempted)) != NULL) {
		goto ok;
	}

	mycore()->nsched_null += 1;
	return NULL;

ok:
	if(p->he.weight == W_LOW) {
		mycore()->nskip_high++;
	}
	if(p == prev) {
		mycore()->nlocal += 1;
	}
	if(debug) {
		printf("%d: running1 %d(%d) vt %lld\n", mycore()->cid, p->pid, p->he.weight, p->he.vruntime);
	}
	if (do_preempt && (p->he.weight == W_LOW)) {
		if (use_runningq) {
			if (p == prev) {
				if (debug)  {
					printf("%d: %d(%d) continue running cid %d\n", mycore()->cid,
				       p->pid, p->he.weight, p->cid);
				}
				if(p_locked != NULL) {
					lock_release(&p_locked->lk);
				}
			} else {
				if (prev != NULL) {
					assert(p_locked != NULL);
					running_clear(ss_global->mh_r, prev);
					lock_release(&p_locked->lk);
					p_locked = NULL;
				}
				assert(p_locked == NULL);
				lock_acquire(&p->lk);
				running_set(ss_global->mh_r, p, mycore()->cid);
				lock_release(&p->lk);
			}
		} else {
			// reset preemtable if switching from high to
			// a low proc, or if preempted
			if(!low || preempted)
				preemptable_set(ss_global->preemptable, mycore()->cid);
		}
	}
	if(mycore()->fd > 0) {
		c_log_append(p);
	}
	return p;
}

// a proc woke up p: enqueue p at the ends of its priority's queue
// XXX use atomic AOR to find low core
void ss_enqueue_rr1(struct task_struct *p) {
	struct core *c = mycore();
	int cid = -1;
	int h = enqueue(p);
	if (do_preempt && p->he.weight == W_HIGH) {
		// XXX see if this core is running a low
		if(c->process != NULL)
			assert(c->process->he.weight == W_LOW);
		if (use_runningq) {
			cid = running_find_and_clear(ss_global->mh_r);
		} else {
			cid = preemptable_find_and_clear(ss_global->preemptable);
		}
		if (cid == -1) {
			mycore()->scan_high = true;
		}
	}
	if (debug) {
		printf("%d: ss_enqueue_rr1 %d(%d) vt %lld dopreempt? cid %d heap %d scan %d\n", c->cid, p->pid, p->he.weight, p->he.vruntime, cid, h, mycore()->scan_high);
	}
	if (cid != -1) {
		atomic_store(&ss_global->cs[cid]->preempted, h);
	}
}

// p yields after it ran for a tick, do nothing until ss_schedule()
void ss_yield_rr1(struct task_struct *p, t_t time_passed) {
	p->runtime += time_passed;
}

// process p goes to sleep
// XXX remove from preemtable and running
void ss_dequeue_rr1(struct task_struct *p, t_t time_passed) {
	p->runtime += time_passed;
	if(debug) {
		printf("%d: %d(%d): dequeue_rr1 %ld\n", mycore()->cid, p->pid, p->he.weight, time_passed);
		//mh_print(p->group->mh);
	}
}
