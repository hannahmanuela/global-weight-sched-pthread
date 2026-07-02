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
// approximate round robin with one or two priority levels using two mheap
//

extern bool debug;
extern bool do_preempt;
extern int num_groups;
extern bool use_runningq;
extern struct sched_state *ss_global;

static int enqueue(struct task_struct *p) {
	if(debug) {
		struct core *c = mycore();
		printf("%d: enqueue_rr %d(%d) in mh %p\n", c->cid, p->pid, p->group->gid, p->group->mh);
	}
	atomic_store(&p->he.vruntime, safe_read_tsc());
	int h = mh_insert_elem(p->group->mh, &p->he);
	return h;
}

static void ss_enqueue_low(struct task_struct *p_l) {
	assert(p_l->group->gid == LOW);
	if (use_runningq) {
		if (debug) {
			printf("%d: %d(%d) remove from runq %d\n", mycore()->cid, p_l->pid, p_l->group->gid, p_l->cid);
		}
		running_clear(ss_global->mh_r, p_l);
	}
	enqueue(p_l);
}

static struct task_struct *ss_schedule_mh_enq(struct mheap *mh, struct task_struct *prev, int hint) {
	struct core *c = mycore();
	bool deq = (prev != NULL) && (prev->group->mh == mh);
	struct task_struct *p = runnable_deq_proc_hint(mh, deq ? prev : NULL, hint);
	if(p != NULL) {
		assert(p->group->mh == mh);
		if(debug) {
			printf("%d: ss_schedule_mh_enq: %d(%d) vt %lld mh %p deq %d\n", c->cid, p->pid, p->group->gid, p->he.vruntime, mh, deq);
		}
		if ((prev != NULL) && !deq) {
			// found a high priority proc to run, add the low-priority prev
			// to the low-priority mheap after removing from running queue.
			ss_enqueue_low(prev);
		}
	}
	return p;
}

// Yield prev, if any, and select new one, if there is a runnable one
struct task_struct *ss_schedule_rr(struct task_struct *prev) {
	struct task_struct *p = NULL;
	struct task_struct *p_locked = NULL;
	bool low = false;
	int preempted = atomic_load(&mycore()->preempted);

	if (do_preempt && preempted != -1) {
		mycore()->npreempted += 1;
		atomic_store(&mycore()->preempted, -1);
	}

	if(prev != NULL) {
		atomic_store(&prev->he.vruntime, safe_read_tsc());
		low = (prev->group->gid == LOW);
		if (debug)
			printf("%d: ss_schedule_rr: low %d preempted by %d prev %d(%d)\n", mycore()->cid, low, preempted, prev->pid, prev->group->gid);
	} else {
		if (debug)
			printf("%d: ss_schedule_rr: low %d preempted by %d idle\n", mycore()->cid, low, preempted);
	}

	// try high priority mh first for runnable proc
	if ((p = ss_schedule_mh_enq(ss_global->mh, prev, preempted)) != NULL) {
		assert(p->group->gid == HIGH);
		goto ok;
	}

	if (num_groups > 1) {
		// no proc found in priority mh but there might be
		// runnable highs that ss_schedule_mh_enq didn't find
		// so scan all heaps of mh first before looking in
		// mh_l.
		mycore()->nscan_all++;
		if ((p = runnable_deq_proc_all_heap(ss_global->mh)) != NULL) {
			assert(p->group->gid == HIGH);
			mycore()->nscan_all_ok++;
			if (prev != NULL) {
				ss_enqueue_low(prev);
			}
			goto ok;
		}

		mycore()->nskip_high++;
		if (use_runningq && (prev != NULL)) {
			// lock prev because it might end up on runnable queue
			// and some core may grab it and add it to the running queue
			// while it is still on the running queue now.
			lock_acquire(&prev->lk);
			p_locked = prev;
		}
		if ((p = ss_schedule_mh_enq(ss_global->mh_l, prev, -1)) != NULL) {
			assert(p->group->gid == LOW);
			goto ok;
		}

		// keep running low proc, if were running one
		if (prev != NULL) {
			assert(prev->group->gid == LOW);
			goto ok;
		}
		assert(p_locked == NULL);
	}
	mycore()->nsched_null += 1;
	return NULL;

ok:
	if(p == prev) {
		mycore()->nlocal += 1;
	}
	if(debug) {
		printf("%d: running %d(%d)\n", mycore()->cid, p->pid, p->group->gid);
	}
	if (do_preempt && (p->group->gid == LOW)) {
		if (use_runningq) {
			if (p == prev) {
				if (debug)  {
					printf("%d: %d(%d) continue running cid %d\n", mycore()->cid,
				       p->pid, p->group->gid, p->cid);
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
void ss_enqueue_rr(struct task_struct *p) {
	struct core *c = mycore();
	int cid = -1;
	int h = enqueue(p);
	if (do_preempt && p->group->gid == HIGH) {
		// XXX see if this core is running a low
		if(c->process != NULL)
			assert(c->process->group->gid == LOW);
		if (use_runningq) {
			cid = running_find_and_clear(ss_global->mh_r);
		} else {
			cid = preemptable_find_and_clear(ss_global->preemptable);
		}
	}
	if (debug) {
		printf("%d: ss_enqueue_rr %d(%d) dopreempt? cid %d heap %p/%d\n", c->cid, p->pid, p->group->gid, cid, p->group->mh, h);
	}
	if (cid != -1) {
		atomic_store(&ss_global->cs[cid]->preempted, h);
	}
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
