#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "vt.h"
#include "util.h"
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
extern int do_preempt;
extern int do_affinity;

static void set_preempt(struct sched_state *ss, struct core *c, struct process *p) {
	while(1) {
		preempt_t pre = atomic_load(&ss->preempt);
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
		if (__atomic_compare_exchange_n(&ss->preempt, &pre, npre, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
			break;
		}
		c->npreempt_retry++;
	}
}

static void reset_preempt(struct sched_state *ss, struct core *c, w_t w) {
	while(1) {
		preempt_t pre = atomic_load(&ss->preempt);
		if(WEIGHT(pre) != w)
			return;
		int n = NCORE(pre);
		w_t w = WEIGHT(pre);
		cid_t cid = CORE(pre);
		if(c->cid == cid) cid = -1;
		if(n == 1) w = MAXWEIGHT;
		preempt_t npre = PREEMPT(n-1, w, cid);
		//printf("reset_preempt: %d %lx (%d, %d, %d)\n", c->cid, npre, n-1, w, cid);
		if (__atomic_compare_exchange_n(&ss->preempt, &pre, npre, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
			break;
		}
		c->npreempt_retry;		
	}
}

static vt_t sub_lag(struct core *c, struct process *p, vt_t wvt, vt_t *lag) {
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
			atomic_fetch_add_explicit(&c->lag_sub_retry, 1, __ATOMIC_RELAXED);
		}
	}
	return vt;
}

static vt_t proc_vt(struct sched_state *ss, struct core *c, struct process *p) {
	vt_t wvt = calc_delta(ss->tick_length, p->he.weight);
	vt_t lag;
	vt_t vt = sub_lag(c, p, wvt, &lag);
	vt_t my_vt = grp_add_vruntime(p, vt) + lag;
	assert(my_vt >= p->he.vruntime);  // overflow?
	return my_vt;
}	


// Select next process to run
bool ss_schedule_gwfs(struct sched_state *ss, struct core *c) {
	struct process *min_proc = NULL;
	if(c->process != NULL) {
		if(debug) {
			printf("%d: schedule yield %d(%d)\n", c->cid, c->process->pid, c->process->group->gid);
		}
		c->process->he.vruntime = proc_vt(ss, c, c->process);
	}

	// XXX kill this case?  for light load we get
	// get affinity by rescheduling c->process
	if(do_affinity && ss->mh->nheap > 1 && c->process) {
		assert(0);
		min_proc = mh_min_affinity(c);
	}

	if (min_proc == NULL) {
		printf("mh_min_proc\n");
		min_proc = mh_min_proc_enq(ss->mh, c, c->process, false);
	}
	if (min_proc == NULL && c->process != NULL) {
		c->nlocal  += 1;
	} else if (min_proc == NULL) {
		c->nsched_null += 1;
		return false;
	} else {
		c->process = min_proc;
	}

	if(debug) {
		printf("%d: schedule %d(%d) vt %lld h %d\n", c->cid, min_proc->pid, min_proc->group->gid, min_proc->he.vruntime, min_proc->h->id);
		mh_print(min_proc->mh);
	}
	if(c->fd > 0) {
		c_log_append(c, min_proc);
	}
	if(do_preempt) {
		set_preempt(ss, c, min_proc);
	}
	return true;
}



static bool ss_preempt(struct sched_state *ss, struct core *c, struct process *p) {
	if(!do_preempt)
		return false;
	preempt_t pre = atomic_load(&ss->preempt);
	w_t w = WEIGHT(pre);
	if(p->he.weight > w) {
		cid_t cid = CORE(pre);
		struct core *c1 = ss->cs[cid];
		lock_acquire(&c1->lk);
		struct process *p1 = c1->process;
		if(p1->he.weight == w) {
			printf("preempt c %d to replace pid %d with pid %d(%d)\n", cid, p1->pid, p->pid, p->he.weight);
		}
		lock_release(&c1->lk);
	}
	return false;
}

static bool ss_preempt_slow(struct sched_state *ss, struct core *c, struct process *p) {
	// vt_t vt = proc_vt(ss, c, p);
	for (int i = 0; i < ss->ncore; i++) {
		struct process *p1 = ss->cs[i]->process;
		if(p1 == NULL) {
			continue;
		}
		if(p1->he.weight < p->he.weight) {
			printf("kick c %d to replace pid %d with pid %d\n", i, p1->pid, p->pid);
			break;
		}
	}
	return false;
}

// Add p to group and make p runnable
void ss_enqueue_gwfs(struct sched_state *ss, struct core *c, struct process *p) {
	struct heap *h = mh_choose_heap(p->mh, c);
	assert(p->h == NULL);

	int old_nthread = atomic_fetch_add(&p->group->nthread, 1);
	if(old_nthread == 0) {  // group has become runnable
		ticks_gettime(p->group->time);
		ticks_sub(p->group->time, p->group->sleepstart);
		ticks_add(p->group->sleeptime, p->group->time);
		vt_t lag = p->group->vruntime - p->group->min_vt_deq;
		vt_t h_min = mh_min_vt(h);
		if(p->group->min_vt_deq > h_min) {
			lag += (p->group->min_vt_deq-h_min);
		}
		vt_t vt = h_min + lag;
		grp_set_vruntime(p, vt);
	}

	if(!ss_preempt(ss, c, p)) {
		p->he.vruntime = proc_vt(ss, c, p);
		mh_add_process(c, p, h);

		if(debug) {
			printf("%d(%d): enqueue nthread %d lh %p vt %lld gvt %lld\n", p->pid, p->group->gid, p->group->nthread, p->h, p->he.vruntime, p->group->vruntime);
			mh_print(p->group->mh);
		}
	}
}

// proc may have run for less than its allocated time; in that
// case adjust the proc's group vruntime.
static void upd_lag(struct sched_state *ss, struct process *p, t_t time_passed) {
	p->runtime += time_passed;
	vt_t vt = calc_delta(time_passed, p->he.weight);
	vt_t wvt = calc_delta(ss->tick_length, p->he.weight);
	if (wvt > vt) {
		grp_add_lag(p, -(wvt-vt));
	}
}

// Yield and enqueue
void ss_yield_gwfs(struct sched_state *ss, struct core *c, struct process *p, t_t time_passed) {
	if(do_preempt)
		reset_preempt(ss, c, p->he.weight);

	upd_lag(ss, p, time_passed);
}

// Process p is not runnable and yields core, which may make
// p's group not runnable
void ss_dequeue_gwfs(struct sched_state *ss, struct core *c, struct process *p, t_t time_passed) {
	if(do_preempt)
		reset_preempt(ss, c, p->he.weight);

	struct heap *h = p->h;
	lock_acquire(&h->lk);

	if(debug) {
		printf("%d(%d): dequeue %ld\n", p->pid, p->group->gid, time_passed);
		mh_print(p->group->mh);
	}

	upd_lag(ss, p, time_passed);

        int old_nthread = atomic_fetch_add(&p->group->nthread, -1);
	if (old_nthread == 1) {
		vt_t h_min = mh_min_vt(p->h);
		p->group->min_vt_deq = h_min;
		ticks_gettime(p->group->sleepstart);
	}

	p->h = NULL;
	c->process = NULL;

	lock_release(&h->lk);
}

