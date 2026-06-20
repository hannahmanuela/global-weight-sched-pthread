#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "vt.h"
#include "util.h"
#include "gw_sched.h"
#include "driver.h"
#include "sched_state.h"
#include "core.h"
#include "group.h"
#include "mheap.h"
#include "runnable.h"
#include "gwfs.h"

//
// approximate global weighted fair sharing with multiheap
//

extern bool debug;
extern bool do_preempt;
extern bool do_affinity;
extern bool delay_yield;
extern struct sched_state *ss_global;

static void set_preempt(struct task_struct *p) {
	struct core *c = mycore();
	while(1) {
		preempt_t pre = atomic_load(&ss_global->preempt);
		if(WEIGHT(pre) < p->he.weight)
			return;

		c->npreempt_set++;

		int n = NCORE(pre);
		w_t w = WEIGHT(pre);
		cid_t cid = CORE(pre);
		preempt_t npre;
		if(w == p->he.weight) n++;
		else {
			w = p->he.weight;
			n = 1;
		}
		npre = PREEMPT(n, w, c->cid);
		//printf("set_preempt: %d %lx (%d, %d, %d)\n", cid, npre, n, w, c->cid);
		if (__atomic_compare_exchange_n(&ss_global->preempt, &pre, npre, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
			break;
		}
		c->npreempt_retry++;
	}
}

static void reset_preempt(w_t w) {
	struct core *c = mycore();
	while(1) {
		preempt_t pre = atomic_load(&ss_global->preempt);
		if(WEIGHT(pre) != w)
			return;

		c->npreempt_clear++;

		int n = NCORE(pre);
		w_t w = WEIGHT(pre);
		cid_t cid = CORE(pre);
		if(c->cid == cid) cid = -1;
		if(n == 1) w = MAXWEIGHT;
		preempt_t npre = PREEMPT(n-1, w, cid);
		//printf("reset_preempt: %d %lx (%d, %d, %d)\n", c->cid, npre, n-1, w, cid);
		if (__atomic_compare_exchange_n(&ss_global->preempt, &pre, npre, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
			break;
		}
		c->npreempt_retry;
	}
}

static vt_t capped_offset(struct task_struct *p, vt_t wvt) {
	vt_t vt = wvt;
	vt_t offset = 0;
	while(1) {
		vt_t v = atomic_load(&p->group->offset);
		assert(v <= 0);
		if(v == 0)
			break;
		if(v < 0) {
			offset = MAX(v, -wvt);
			if (__atomic_compare_exchange_n(&p->group->offset, &v, v-offset, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
				break;
			}
			atomic_fetch_add_explicit(&mycore()->offset_sub_retry, 1, __ATOMIC_RELAXED);
		}
	}
	return offset;
}

static vt_t proc_vt(struct task_struct *p) {
	vt_t wvt = calc_delta(ss_global->tick_length, p->he.weight);
	vt_t offset = capped_offset(p, wvt);
	vt_t vt = wvt + offset;
	vt_t my_vt = grp_add_vruntime(p, vt) + offset;
	assert(my_vt >= p->he.vruntime);  // overflow?
	return my_vt;
}	

// proc may have run for less than its allocated time; in that
// case adjust the proc's group vruntime.
static void upd_offset(struct task_struct *p, t_t time_passed) {
	p->runtime += time_passed;
	vt_t vt = calc_delta(time_passed, p->he.weight);
	vt_t wvt = calc_delta(ss_global->tick_length, p->he.weight);
	if (wvt > vt) {
		grp_add_offset(p, -(wvt-vt));
	}
}

void ss_account_gwfs(struct task_struct *p, u64 time_passed) {
	upd_offset(p, time_passed);
}

// XXX kernel API: why runq?
struct task_struct *ss_schedule_gwfs(struct rq *rq, struct task_struct *prev) {
	struct task_struct *min_proc = NULL;

	if(prev) {
		// XXX kernel API: why isn't this in ss_account_gwfs?
		prev->he.vruntime = proc_vt(prev);
		if(debug) {
			printf("%d: schedule yield %d(%d) vt %d gvt %ld\n", mycore()->cid, prev->pid, prev->group->gid, prev->he.vruntime, prev->group->vruntime);
		}
	}

	min_proc = runnable_deq_proc(ss_global->mh, prev);
	if (min_proc == NULL) {
		mycore()->nsched_null += 1;
		return NULL;
	} else if (min_proc == prev) {
		mycore()->nlocal  += 1;
	}
	if(debug) {
		printf("%d: schedule %d(%d) vt %lld\n", mycore()->cid, min_proc->pid, min_proc->group->gid, min_proc->he.vruntime);
		proc_mh_print(min_proc->group->mh);
	}
	if(mycore()->fd > 0) {
		c_log_append(min_proc);
	}
	if(do_preempt) {
		set_preempt(min_proc);
	}
	return min_proc;
}

struct task_struct *ss_account_schedule_gwfs(struct task_struct *prev) {
	if (prev != NULL)
		ss_account_gwfs(prev, ss_global->tick_length);
	return ss_schedule_gwfs(NULL, prev);
}

static bool ss_preempt(struct task_struct *p) {
	if(!do_preempt)
		return false;
	preempt_t pre = atomic_load(&ss_global->preempt);
	w_t w = WEIGHT(pre);
	if(p->he.weight > w) {
		cid_t cid = CORE(pre);
		struct core *c1 = ss_global->cs[cid];
		lock_acquire(&c1->lk);
		struct task_struct *p1 = c1->process;
		if(p1->he.weight == w) {
			printf("preempt c %d to replace pid %d with pid %d(%d)\n", cid, p1->pid, p->pid, p->he.weight);
			c1->npreempted += 1;
		}	
		lock_release(&c1->lk);
	}
	return false;
}

static bool ss_preempt_slow(struct task_struct *p) {
	// vt_t vt = proc_vt(c, p);
	for (int i = 0; i < ss_global->ncore; i++) {
		struct task_struct *p1 = ss_global->cs[i]->process;
		if(p1 == NULL) {
			continue;
		}
		if(p1->he.weight < p->he.weight) {
			printf("kick c %d to replace pid %d with pid %d\n", i, p1->pid, p->pid);
			ss_global->cs[i]->npreempted += 1;
			break;
		}
	}
	return false;
}


// XXX kernel API: no min?
static vt_t min_vt(struct heap *h) {
	vt_t h_min = mh_min_vt(h);
	if (h_min == DUMMY) {
		h_min = mh_last_vt(h);
		// XXX fix me
		/*
		if (c->process && c->process->he.vruntime > h_min) {
			h_min = c->process->he.vruntime;
		}
		*/
	}
	return h_min;
}

static void account_wakeup_gwfs(struct task_struct *p) {
	int old_nthread = atomic_fetch_add(&p->group->nthread, 1);
	if(old_nthread == 0) {  // group has become runnable
		ticks_gettime(p->group->time);
		ticks_sub(p->group->time, p->group->sleepstart);
		ticks_add(p->group->sleeptime, p->group->time);
		vt_t offset = p->group->vruntime - p->group->min_vt_deq;
                // XXX kernel API: defaults to heap 0 for min
		vt_t h_min = min_vt(mh_heap(p->group->mh, 0));
		if(p->group->min_vt_deq > h_min) {
			offset += (p->group->min_vt_deq-h_min);
		}
		vt_t vt = h_min + offset;
		grp_set_vruntime(p, vt);
	}
}

static void put_task_in_rq_gwfs(struct task_struct *p) {
	p->he.vruntime = proc_vt(p);
	mh_insert_elem(p->group->mh, &p->he);
	if(debug) {
		printf("%d(%d): enqueue nthread %d lh %p vt %lld gvt %lld\n", p->pid, p->group->gid, p->group->nthread, p->h, p->he.vruntime, p->group->vruntime);
		proc_mh_print(p->group->mh);
	}
}

// Add p to group and make p runnable
void ss_enqueue_gwfs(struct task_struct *p) {
	account_wakeup_gwfs(p);
	if(!ss_preempt(p)) {
		put_task_in_rq_gwfs(p);
	}
}

// Yield and enqueue
void ss_yield_gwfs(struct task_struct *p, t_t time_passed) {
	if(do_preempt)
		reset_preempt(p->he.weight);
	if(!delay_yield) {
		upd_offset(p, time_passed);
		p->he.vruntime = proc_vt(p);
		mh_insert_elem(p->group->mh, &p->he);
	}
}

// XXX kernel API: who does upd_offset()? gwfs has dequeue do it
// XXX kernel API: kernel update_curr_gw isn't part of interface?
static void account_sleep_gwfs(struct task_struct *p) {
	if(do_preempt)
		reset_preempt(p->he.weight);

        int old_nthread = atomic_fetch_add(&p->group->nthread, -1);
	if (old_nthread == 1) {
		// XXX kernel impl uses 0 as a default instead of p->h
		vt_t h_min = min_vt(mh_heap(p->group->mh, 0));
		p->group->min_vt_deq = h_min;
		ticks_gettime(p->group->sleepstart);
	}

	mycore()->process = NULL;
}

// Process p is not runnable and yields core, which may make
// p's group not runnable
void ss_dequeue_gwfs(struct task_struct *p, t_t time_passed) {
	if(debug) {
		printf("%d(%d): dequeue %ld\n", p->pid, p->group->gid, time_passed);
		proc_mh_print(p->group->mh);
	}
	upd_offset(p, time_passed);
	account_sleep_gwfs(p);
}

static void init_gwfs() {
}

// XXX why have yield
const struct gw_scheduler gw_sched_wfs = {
        .name           = "wfs",
        .init           = init_gwfs,
        .account_wakeup   = account_wakeup_gwfs,
        .account_sleep    = account_sleep_gwfs,
        .put_task_in_rq   = put_task_in_rq_gwfs,
        .take_task_from_rq = NULL,
        .charge_vt        = NULL,
        .account        = ss_account_gwfs,
        .schedule       = ss_schedule_gwfs,
        .yield          = NULL,
        .pick_idle_target = NULL,
        .any_queued     = NULL,
        .set_nheaps     = NULL,
};
