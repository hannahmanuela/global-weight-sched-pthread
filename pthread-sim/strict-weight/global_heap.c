#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdbool.h>

#include "vt.h"
#include "util.h"
#include "ticks.h"
#include "driver.h"
#include "core.h"
#include "lheap.h"
#include "group.h"
#include "mheap.h"

bool debug;
bool with_tsc;

// Select next process to run
struct process *schedule(struct core *c, struct mheap *mh) {
	//if (c->current_process && mh_is_min(c->current_process))
	// c->hit++;
	struct process *min_proc = mh_min_proc(c, mh);
	if (min_proc == NULL) {
		return NULL;
	}

	pthread_rwlock_wrlock(&min_proc->proc_lock);

	if(debug) {
		printf("%d: schedule %d(%d) vt %u\n", c->cid, min_proc->pid, min_proc->group->gid, min_proc->he.vruntime);
		mh_print(min_proc->mh);
	}

        // atomic_fetch_add(&min_proc->group->nqueued, -1);  // for debugging 
	
	pthread_rwlock_unlock(&min_proc->proc_lock);

	return min_proc;
}

// Add p to group and make p runnable
void enqueue(struct core *c, struct process *p) {
	struct lheap *lh = mh_choose_heap(c, p->mh);

	pthread_rwlock_wrlock(&p->proc_lock);
	assert(p->lh == NULL);

	int old_nthread = atomic_fetch_add(&p->group->nthread, 1);

	if(old_nthread == 0) {  // group has become runnable
		ticks_gettime(p->group->time);
		ticks_sub(p->group->time, p->group->sleepstart);
		ticks_add(p->group->sleeptime, p->group->time);
	}

	vt_t wvt = grp_slot(p, old_nthread+1);
	proc_set_init_vruntime(p, mh_min_vt(lh) + wvt);

	mh_add_process(c, p, lh);
	// atomic_fetch_add(&p->group->nqueued, 1);    // for debugging

	if(debug) {
		printf("%d(%d): enqueue nthread %d lh %p vt %u\n", p->pid, p->group->gid, p->group->nthread, p->lh, p->he.vruntime);
		mh_print(p->group->mh);
	}


	pthread_rwlock_unlock(&p->proc_lock);
	lh_unlock(p->lh);
}

// Process p yields core
static void yieldL(struct process *p, vt_t time_passed, vt_t vt) {
	p->runtime += time_passed;
	proc_add_vruntime(p, vt);
}

// Yield and enqueue
void yield(struct core *c, struct process *p, t_t time_passed) {
	struct lheap *lh = mh_choose_heap(c, p->mh);
	pthread_rwlock_wrlock(&p->proc_lock);

	int nthread = atomic_load(&p->group->nthread);

	vt_t vt = calc_delta(time_passed, p->he.weight);
	vt += grp_slot(p, nthread);
	yieldL(p, time_passed, vt);

	mh_add_process(c, p, lh);
	// atomic_fetch_add(&p->group->nqueued, 1);    // for debugging

	if(debug) {
		printf("%d(%d): yield time_passed %ld nt %d w %d vt %u\n", p->pid, p->group->gid, time_passed, p->group->nthread, p->he.weight, p->he.vruntime);
		mh_print(p->group->mh);
	}

	pthread_rwlock_unlock(&p->proc_lock);
	lh_unlock(p->lh);
}

// Process p is not runnable and yields core, which may make
// p's group not runnable
void dequeue(struct core *c, struct process *p, t_t time_passed) {
	struct lheap *lh = p->lh;
	lh_lock_timed(c, lh);
	pthread_rwlock_wrlock(&p->proc_lock);

	if(debug) {
		printf("%d(%d): dequeue %ld\n", p->pid, p->group->gid, time_passed);
		mh_print(p->group->mh);
	}

	vt_t vt = calc_delta(time_passed, p->he.weight);
	yieldL(p, time_passed, vt);
	proc_lag_vruntime(p, mh_min_vt(lh));

	p->lh = NULL;
	assert(p->group->nthread >= p->group->nqueued);

        int old_nthread = atomic_fetch_add(&p->group->nthread, -1);
	if (old_nthread == 1) {
		ticks_gettime(p->group->sleepstart);
	}
	pthread_rwlock_unlock(&p->proc_lock);
	lh_unlock(lh);
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
		grp_stats(grps[i], tot);
	}
	printf("\n=\n");
}
