#include <assert.h>
#include <limits.h>
#include <stdio.h>

#include "vt.h"
#include "driver.h"
#include "lheap.h"
#include "group.h"
#include "mheap.h"

bool debug;

// Select next process to run
struct process *schedule(int core, struct mheap *mh) {
	struct process *min_proc = mh_min_proc(mh);
	if (min_proc == NULL) {
		return NULL;
	}

        // mh_min_proc returns with proc lock held and proc
	// removed from mheap.
    
	if(debug) {
		printf("%d: schedule %d(%d) vt %d\n", core, min_proc->pid, min_proc->group->gid, min_proc->vruntime);
		mh_print(min_proc->mh);
	}

	min_proc->group->nqueued -= 1;
	min_proc->group->nrunning += 1;
	
	pthread_rwlock_unlock(&min_proc->proc_lock);

	return min_proc;
}

// Add p to group and make p runnable
void enqueue(struct process *p) {
	struct lock_heap *lh = mh_choose_heap(p->mh);

	pthread_rwlock_wrlock(&p->proc_lock);
	assert(p->lh == NULL);

	if(debug) {
		printf("%d(%d): enqueue nthread %d lh %p min %d\n", p->pid, p->group->gid, p->group->nthread, p->lh, mh_min(lh));
		mh_print(p->group->mh);
	}

	if(p->group->nthread == 0) {  // group is runnable
		ticks_gettime(p->group->time);
		ticks_sub(p->group->time, p->group->sleepstart);
		ticks_add(p->group->sleeptime, p->group->time);
	}

	vt_t wvt = calc_delta(p->mh->tick_length, p->weight) * p->group->nthread;
	proc_set_init_vruntime(p, mh_min(lh) + wvt);
	mh_add_process(p, lh);
	p->group->nqueued += 1;
	p->group->nthread += 1;

	pthread_rwlock_unlock(&p->proc_lock);
	lh_unlock(p->lh);
}

// Process p yields core
static void yieldL(struct process *p, vt_t time_passed, vt_t vt) {
	p->group->runtime += time_passed;
	p->group->nrunning -= 1;
	proc_add_vruntime(p, vt);
}

// Yield and enqueue
void yield(struct process *p, t_t time_passed) {
	struct lock_heap *lh = mh_choose_heap(p->mh);
	pthread_rwlock_wrlock(&p->proc_lock);

	vt_t vt = calc_delta(time_passed, p->weight);
	vt += calc_delta(p->mh->tick_length, p->weight) * (p->group->nthread-1);
	yieldL(p, time_passed, vt);
	p->group->nqueued += 1;
	mh_add_process(p, lh);

	if(debug) {
		printf("%d(%d): yield time_passed %d nt %d w %d vt %d\n", p->pid, p->group->gid, time_passed, p->group->nthread, p->weight, p->vruntime);
		mh_print(p->group->mh);
	}

	pthread_rwlock_unlock(&p->proc_lock);
	lh_unlock(p->lh);
}

// Process p is not runnable and yields core, which may make
// p's group not runnable
void dequeue(struct process *p, t_t time_passed) {
	struct lock_heap *lh = p->lh;
	lh_lock_timed(lh);
	pthread_rwlock_wrlock(&p->proc_lock);
	

	if(debug) {
		printf("%d(%d): dequeue %d\n", p->pid, p->group->gid, time_passed);
		mh_print(p->group->mh);
	}

	vt_t vt = calc_delta(time_passed, p->weight);
	yieldL(p, time_passed, vt);
	proc_lag_vruntime(p, mh_min(lh));

	p->lh = NULL;
	assert(p->group->nthread >= p->group->nqueued);
	p->group->nthread -= 1;

	if (grp_is_sleep(p->group)) {
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
	printf("=\n");
}
