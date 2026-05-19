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
#include "gwfs.h"

//
// approximate global weighted fair sharing with multiheap
//

extern bool debug;
extern bool do_preempt;
extern bool do_affinity;
extern bool delay_yield;
extern struct sched_state *ss_global;

static void set_preempt(struct core *c, struct task_struct *p) {
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

static void reset_preempt(struct core *c, w_t w) {
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

static vt_t sub_lag(struct task_struct *p, vt_t wvt, vt_t *lag) {
	vt_t vt = wvt;
	*lag = 0;
	while(1) {
		vt_t v = atomic_load(&p->group->lag);
		*lag = v;
		assert(v <= 0);
		if(v == 0)
			break;
		if(v < 0) {
			if(v < -wvt) {
				*lag = -wvt;
			}
			vt = wvt + *lag;
			if (__atomic_compare_exchange_n(&p->group->lag, &v, v-*lag, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
				break;
			}
			struct core *c = get_core();
			atomic_fetch_add_explicit(&c->lag_sub_retry, 1, __ATOMIC_RELAXED);
		}
	}
	return vt;
}

static vt_t proc_vt(struct task_struct *p) {
	vt_t wvt = calc_delta(ss_global->tick_length, p->he.weight);
	vt_t lag;
	vt_t vt = sub_lag(p, wvt, &lag);
	vt_t my_vt = grp_add_vruntime(p, vt) + lag;
	assert(my_vt >= p->he.vruntime);  // overflow?
	return my_vt;
}	

// proc may have run for less than its allocated time; in that
// case adjust the proc's group vruntime.
static void upd_lag(struct task_struct *p, t_t time_passed) {
	p->runtime += time_passed;
	vt_t vt = calc_delta(time_passed, p->he.weight);
	vt_t wvt = calc_delta(ss_global->tick_length, p->he.weight);
	if (wvt > vt) {
		grp_add_lag(p, -(wvt-vt));
	}
}

void ss_account_gwfs(struct task_struct *p, u64 time_passed) {
	upd_lag(p, time_passed);
}

// XXX why runq?
struct task_struct *ss_schedule_gwfs(struct rq *rq, struct task_struct *prev) {
	struct task_struct *min_proc = NULL;

	// XXX why isn't this in ss_account_gwfs?
	if(prev) prev->he.vruntime = proc_vt(prev);

	if(debug) {
		printf("%d: schedule yield %d(%d) vt %d gvt %ld\n", get_core()->cid, prev->pid, prev->group->gid, prev->he.vruntime, prev->group->vruntime);
	}

	// XXX kill this case?  for light load we get
	// get affinity by rescheduling prev
	if(do_affinity && ss_global->mh->nheap > 1 && prev) {
		assert(0);
		min_proc = mh_min_affinity(get_core());
	}

	if (min_proc == NULL) {
		min_proc = mh_min_proc_enq(ss_global->mh, prev, false);
	}
	if (min_proc == NULL && prev != NULL) {
		get_core()->nlocal  += 1;
		min_proc = prev;  // for debug
	} else if (min_proc == NULL) {
		get_core()->nsched_null += 1;
		return NULL;
	}

	if(debug) {
		printf("%d: schedule %d(%d) vt %lld\n", get_core()->cid, min_proc->pid, min_proc->group->gid, min_proc->he.vruntime);
		mh_print(min_proc->mh);
	}
	if(get_core()->fd > 0) {
		c_log_append(get_core(), min_proc);
	}
	if(do_preempt) {
		set_preempt(get_core(), min_proc);
	}
	return min_proc;
}

bool ss_account_schedule_gwfs() {
	struct core *c = get_core();
	if (c->process != NULL)
		ss_account_gwfs(c->process, ss_global->tick_length);
	c->process = ss_schedule_gwfs(NULL, c->process);
	return c->process != NULL;
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


// XXX min 
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

// XXX should min_vt take the heap that p will be inserted in?
static void account_wakeup_gwfs(struct task_struct *p) {
	int old_nthread = atomic_fetch_add(&p->group->nthread, 1);
	if(old_nthread == 0) {  // group has become runnable
		ticks_gettime(p->group->time);
		ticks_sub(p->group->time, p->group->sleepstart);
		ticks_add(p->group->sleeptime, p->group->time);
		vt_t lag = p->group->vruntime - p->group->min_vt_deq;
		vt_t h_min = min_vt(mh_heap(p->mh, 0));
		if(p->group->min_vt_deq > h_min) {
			lag += (p->group->min_vt_deq-h_min);
		}
		vt_t vt = h_min + lag;
		grp_set_vruntime(p, vt);
	}
}

static void put_task_in_rq_gwfs(struct task_struct *p) {
	// XXX run test and driver group setup in pthread
	struct heap *h = mh_choose_heap(p->mh);
	assert(p->h == NULL);
	p->he.vruntime = proc_vt(p);
	mh_add_process(p, h);
	lock_release(&h->lk);

	if(debug) {
		printf("%d(%d): enqueue nthread %d lh %p vt %lld gvt %lld\n", p->pid, p->group->gid, p->group->nthread, p->h, p->he.vruntime, p->group->vruntime);
		mh_print(p->group->mh);
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
	struct core *c = get_core();
	if(do_preempt)
		reset_preempt(c, p->he.weight);

	assert(p == c->process);
	upd_lag(p, time_passed);
	p->he.vruntime = proc_vt(p);
	if(!delay_yield) {
		struct heap *h = mh_choose_heap(p->mh);
		mh_add_process(p, h);
		lock_release(&h->lk);
		c->process = NULL;
	}
}

// XXX why is time_passed not an argument?
// XXX who does upd_lag()
// XXX update_curr_gw isn't part of interface?
static void account_sleep_gwfs(struct task_struct *p) {
	struct core *c = get_core();
	if(do_preempt)
		reset_preempt(c, p->he.weight);

	struct heap *h = p->h;
	lock_acquire(&h->lk);

        int old_nthread = atomic_fetch_add(&p->group->nthread, -1);
	if (old_nthread == 1) {
		vt_t h_min = min_vt(p->h);
		p->group->min_vt_deq = h_min;
		ticks_gettime(p->group->sleepstart);
	}

	p->h = NULL;
	c->process = NULL;

	lock_release(&h->lk);	
}

// Process p is not runnable and yields core, which may make
// p's group not runnable
void ss_dequeue_gwfs(struct task_struct *p, t_t time_passed) {
	if(debug) {
		printf("%d(%d): dequeue %ld\n", p->pid, p->group->gid, time_passed);
		mh_print(p->group->mh);
	}
	upd_lag(p, time_passed);
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
