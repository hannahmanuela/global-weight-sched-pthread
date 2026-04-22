#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>

#include "vt.h"
#include "util.h"
#include "driver.h"
#include "global_heap.h"
#include "core.h"
#include "group.h"
#include "mheap.h"
#include "mvalue.h"

//
// global_heap with weights
//

bool debug = false;
bool do_affinity = false;
bool do_preempt = false;

struct global_heap *gh_new(int tick_length, int nheap, struct core *cs[], int ncore, bool using_mv) {
	struct global_heap *gh = aligned_alloc(CACHE_LINE_SZ, sizeof(struct global_heap));
	gh->tick_length = tick_length;
	gh->mh = mh_new(nheap);
	gh->using_mv = using_mv;
	gh->mv = using_mv ? mv_new(nheap) : NULL;
	gh->cs = cs;
	gh->ncore = ncore;
	gh->preempt = PREEMPT(0, MAXWEIGHT, 0);
	return gh;
}

static void set_preempt(struct global_heap *gh, struct core *c, struct process *p) {
	while(1) {
		preempt_t pre = atomic_load(&gh->preempt);
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
		if (__atomic_compare_exchange_n(&gh->preempt, &pre, npre, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
			break;
		}
		c->npreempt_retry++;
	}
}

static void reset_preempt(struct global_heap *gh, struct core *c, w_t w) {
	while(1) {
		preempt_t pre = atomic_load(&gh->preempt);
		if(WEIGHT(pre) != w)
			return;
		int n = NCORE(pre);
		w_t w = WEIGHT(pre);
		cid_t cid = CORE(pre);
		if(c->cid == cid) cid = -1;
		if(n == 1) w = MAXWEIGHT;
		preempt_t npre = PREEMPT(n-1, w, cid);
		//printf("reset_preempt: %d %lx (%d, %d, %d)\n", c->cid, npre, n-1, w, cid);
		if (__atomic_compare_exchange_n(&gh->preempt, &pre, npre, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
			break;
		}
		c->npreempt_retry;		
	}
}

// Select next process to run
struct process *gh_schedule(struct global_heap *gh, struct core *c) {
	struct process *min_proc = NULL;
	if(do_affinity && gh->mh->nheap > 1 && c->process) {
		min_proc = mh_min_affinity(c);
	}
	if (min_proc == NULL) {
		min_proc = mh_min_proc(gh->mh, c);
	}
	if (min_proc == NULL) {
		c->process = NULL;
		return NULL;
	}

	if(debug) {
		printf("%d: schedule %d(%d) vt %lld h %d\n", c->cid, min_proc->pid, min_proc->group->gid, min_proc->he.vruntime, min_proc->h->id);
		mh_print(min_proc->mh);
	}
	if(c->fd > 0) {
		c_log_append(c, min_proc);
	}
	if(do_preempt) {
		set_preempt(gh, c, min_proc);
	}
	c->process = min_proc;
	return min_proc;
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

static vt_t proc_vt(struct global_heap *gh, struct core *c, struct process *p) {
	vt_t wvt = calc_delta(gh->tick_length, p->he.weight);
	vt_t lag;
	vt_t vt = sub_lag(c, p, wvt, &lag);
	vt_t my_vt = grp_add_vruntime(p, vt) + lag;
	assert(my_vt >= p->he.vruntime);  // overflow?
	return my_vt;
}	

static void enq_proc_vt(struct global_heap *gh, struct core *c, struct process *p, struct heap *h) {
	p->he.vruntime = proc_vt(gh, c, p);
	mh_add_process(c, p, h);
}

static bool gh_preempt(struct global_heap *gh, struct core *c, struct process *p) {
	if(!do_preempt)
		return false;
	preempt_t pre = atomic_load(&gh->preempt);
	w_t w = WEIGHT(pre);
	if(p->he.weight > w) {
		cid_t cid = CORE(pre);
		struct core *c1 = gh->cs[cid];
		lock_acquire(&c1->lk);
		struct process *p1 = c1->process;
		if(p1->he.weight == w) {
			printf("preempt c %d to replace pid %d with pid %d(%d)\n", cid, p1->pid, p->pid, p->he.weight);
		}
		lock_release(&c1->lk);
	}
	return false;
}

static bool gh_preempt_slow(struct global_heap *gh, struct core *c, struct process *p) {
	// vt_t vt = proc_vt(gh, c, p);
	for (int i = 0; i < gh->ncore; i++) {
		struct process *p1 = gh->cs[i]->process;
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
void gh_enqueue(struct global_heap *gh, struct core *c, struct process *p) {
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

	if(!gh_preempt(gh, c, p)) {
		enq_proc_vt(gh, c, p, h);

		if(debug) {
			printf("%d(%d): enqueue nthread %d lh %p vt %lld gvt %lld\n", p->pid, p->group->gid, p->group->nthread, p->h, p->he.vruntime, p->group->vruntime);
			mh_print(p->group->mh);
		}
	}
}

// proc may have run for less than its allocated time; in that
// case adjust the proc's group vruntime.
static void upd_lag(struct global_heap *gh, struct process *p, t_t time_passed) {
	p->runtime += time_passed;
	vt_t vt = calc_delta(time_passed, p->he.weight);
	vt_t wvt = calc_delta(gh->tick_length, p->he.weight);
	if (wvt > vt) {
		grp_add_lag(p, -(wvt-vt));
	}
}

// Yield and enqueue
void gh_yield(struct global_heap *gh, struct core *c, struct process *p, t_t time_passed) {
	if(do_preempt)
		reset_preempt(gh, c, p->he.weight);

	upd_lag(gh, p, time_passed);

	struct heap *h = mh_choose_heap(p->mh, c);

	enq_proc_vt(gh, c, p, h);

	if(debug) {
		printf("%d(%d): yield time_passed %ld nt %d w %d vt %lld h %d\n", p->pid, p->group->gid, time_passed, p->group->nthread, p->he.weight, p->he.vruntime, h->id);
		mh_print(p->group->mh);
	}
}

// Process p is not runnable and yields core, which may make
// p's group not runnable
void gh_dequeue(struct global_heap *gh, struct core *c, struct process *p, t_t time_passed) {
	if(do_preempt)
		reset_preempt(gh, c, p->he.weight);

	struct heap *h = p->h;
	lock_acquire(&h->lk);

	if(debug) {
		printf("%d(%d): dequeue %ld\n", p->pid, p->group->gid, time_passed);
		mh_print(p->group->mh);
	}

	upd_lag(gh, p, time_passed);

        int old_nthread = atomic_fetch_add(&p->group->nthread, -1);
	if (old_nthread == 1) {
		vt_t h_min = mh_min_vt(p->h);
		p->group->min_vt_deq = h_min;
		ticks_gettime(p->group->sleepstart);
	}

	p->h = NULL;

	lock_release(&h->lk);
}

void gh_print(struct global_heap *gh, struct group *grps[], int n) {
	mh_print(gh->mh);
	printf("= groups %d:\n", n);
	for(int i = 0; i < n; i++) {
		printf("  "); grp_print(grps[i]); printf("\n");
	}
	printf("=\n");
	printf("= cores %d:\n", gh->ncore);
	for(int i = 0; i < gh->ncore; i++) {
		printf("  %d: ", i); core_print(gh->cs[i]); printf("\n");
	}
	printf("=\n");
}

void gh_stats(struct global_heap *gh, struct group *grps[], int n) {
	t_t *ticks = new_ticks();
	ticks_gettime(ticks);
	t_t tot = ticks_sum(ticks);
	ticks_getwork(ticks);
	t_t work = ticks_sum(ticks);
	ticks_getidle(ticks);
	t_t idle = ticks_sum(ticks);
	printf("= stats total ticks %ld us work %ld us idle %ld us\n", tot, work, idle);
	for(int i = 0; i < n; i++) {
		grp_stats(grps[i], tot); printf("\n");
	}
}
