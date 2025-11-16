#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdbool.h>

#include "vt.h"
#include "util.h"
#include "ticks.h"
#include "driver.h"
#include "core.h"
#include "group.h"
#include "mheap.h"

bool debug;

// Select next process to run
struct process *schedule(struct core *c, struct mheap *mh) {
	//if (c->current_process && mh_is_min(c->current_process))
	// c->hit++;
	struct process *min_proc = mh_min_proc(c, mh);
	if (min_proc == NULL) {
		return NULL;
	}

	if(debug) {
		printf("%d: schedule %d(%d) vt %u\n", c->cid, min_proc->pid, min_proc->group->gid, min_proc->he.vruntime);
		mh_print(min_proc->mh);
	}

	return min_proc;
}

// Add p to group and make p runnable
void enqueue(struct core *c, struct process *p) {
	struct heap *h = mh_choose_heap(c, p->mh);

	assert(p->h == NULL);

	int old_nthread = atomic_fetch_add(&p->group->nthread, 1);
	if(old_nthread == 0) {  // group has become runnable
		ticks_gettime(p->group->time);
		ticks_sub(p->group->time, p->group->sleepstart);
		ticks_add(p->group->sleeptime, p->group->time);
		vt_t vt = mh_min_vt(h) + p->group->lag;
		grp_set_vruntime(p, vt);
	}

	vt_t wvt = calc_delta(p->mh->tick_length, p->he.weight);
	vt_t my_vt = grp_add_vruntime(p, wvt);
	p->he.vruntime = my_vt;
	mh_add_process(c, p, h);

	if(debug) {
		printf("%d(%d): enqueue nthread %d lh %p vt %u gvt %d\n", p->pid, p->group->gid, p->group->nthread, p->h, p->he.vruntime, p->group->vruntime);
		mh_print(p->group->mh);
	}

	lock_release(&p->h->lk);
}

// proc may have run for less than its allocated time; in that
// case adjust the proc's group vruntime.
static void grp_adjust_vruntime(struct process *p, t_t time_passed) {
	p->runtime += time_passed;
	vt_t vt = calc_delta(time_passed, p->he.weight);
	vt_t wvt = calc_delta(p->mh->tick_length, p->he.weight);
	if (wvt > vt) {
		grp_add_vruntime(p, -(wvt-vt));
	}
}

// Yield and enqueue
void yield(struct core *c, struct process *p, t_t time_passed) {
	grp_adjust_vruntime(p, time_passed);

	vt_t wvt = calc_delta(p->mh->tick_length, p->he.weight);
	vt_t my_vt = grp_add_vruntime(p, wvt);
	p->he.vruntime = my_vt;

	struct heap *h = mh_choose_heap(c, p->mh);
	
	mh_add_process(c, p, h);

	if(debug) {
		printf("%d(%d): yield time_passed %ld nt %d w %d vt %u\n", p->pid, p->group->gid, time_passed, p->group->nthread, p->he.weight, p->he.vruntime);
		mh_print(p->group->mh);
	}

	lock_release(&p->h->lk);
}

// Process p is not runnable and yields core, which may make
// p's group not runnable
void dequeue(struct core *c, struct process *p, t_t time_passed) {
	struct heap *h = p->h;
	lock_acquire(&h->lk);

	if(debug) {
		printf("%d(%d): dequeue %ld\n", p->pid, p->group->gid, time_passed);
		mh_print(p->group->mh);
	}

	grp_adjust_vruntime(p, time_passed);

        int old_nthread = atomic_fetch_add(&p->group->nthread, -1);
	if (old_nthread == 1) {
		vt_t h_min = mh_min_vt(p->h);
		p->group->lag = p->group->vruntime - h_min;
		ticks_gettime(p->group->sleepstart);
	}

	p->h = NULL;

	lock_release(&h->lk);
}

void print(struct mheap *mh, struct group *grps[], int n) {
	mh_print(mh);
	printf("= groups %d:\n", n);
	for(int i = 0; i < n; i++) {
		printf("  "); grp_print(grps[i]); printf("\n");
	}
	printf("=\n");
}

void stats(struct group *grps[], int n) {
	t_t *ticks = new_ticks();
	ticks_gettime(ticks);
	t_t tot = ticks_sum(ticks);
	ticks_getwork(ticks);
	t_t work = ticks_sum(ticks);
	ticks_getidle(ticks);
	t_t idle = ticks_sum(ticks);
	printf("= stats total ticks %ld us work %ld us idle %ld us\n", tot, work, idle);
	for(int i = 0; i < n; i++) {
		printf("  "); grp_stats(grps[i], tot); printf("\n");
	}
}
