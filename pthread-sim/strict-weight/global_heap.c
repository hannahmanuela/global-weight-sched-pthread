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

        // gl_min_group returns with heap and proc lock held
    
	if(debug) {
		printf("%d: schedule %d(%d) vt %d\n", core, min_proc->process_id, min_proc->group->group_id, min_proc->vruntime);
		mh_print(min_proc->mh);
	}

	// select the next process
	// struct process *next_p = grp_deq_process(min_group);
	// assert(next_p != NULL);
	min_proc->group->nqueued -= 1;
	min_proc->group->nrunning += 1;
	
	// must be after grp_deq_process, since it may empty the proc queue
	// heap_fix_index(min_proc->lh->heap, &min_group->heap_elem);

	pthread_rwlock_unlock(&min_proc->proc_lock);

	return min_proc;
}

// Add p to group and make p runnable
void enqueue(struct process *p) {
	struct lock_heap *lh = mh_choose_heap(p->mh);

	pthread_rwlock_wrlock(&p->proc_lock);
	assert(p->lh == NULL);

	grp_add_process(p);

	p->weight = p->group->weight/p->group->nthread;
		 
	if(debug) {
		printf("%d(%d): enqueue nthread %d lh%p\n", p->process_id, p->group->group_id, p->group->nthread, p->group->lh);
		mh_print(p->group->mh);
	}

	if(p->group->nthread == 1) {  // group is runnable
		ticks_gettime(p->group->time);
		ticks_sub(p->group->time, p->group->sleepstart);
		ticks_add(p->group->sleeptime, p->group->time);
	}

	proc_set_init_vruntime(p, mh_min(lh));
	mh_add_process(p, lh);
	p->group->nqueued += 1;

	pthread_rwlock_unlock(&p->proc_lock);
	lh_unlock(p->lh);
}

// Process p yields core
static void yieldL(struct process *p, int time_passed) {
	p->group->runtime += time_passed;
	p->group->nrunning -= 1;
	proc_upd_vruntime(p, time_passed);
}

// Yield and enqueue
void yield(struct process *p, t_t time_passed) {
	struct lock_heap *lh = mh_choose_heap(p->mh);
	pthread_rwlock_wrlock(&p->proc_lock);

	int w = p->weight;
	p->weight = p->group->weight/p->group->nthread;
	vt_t vt = calc_delta(p->mh->tick_length, p->weight);
	if(w != p->weight && p->vruntime != 0) {
		printf("%d(%d): was scheduled too early vt %d vt %d weight; %d %d\n", p->process_id, p->group->group_id, p->vruntime, vt, w, p->weight);
	}

	yieldL(p, time_passed);
	p->group->nqueued += 1;
	mh_add_process(p, lh);

	if(debug) {
		printf("%d(%d): yield time_passed %d %d\n", p->process_id, p->group->group_id, time_passed, p->group->nthread);
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
		printf("%d(%d): dequeue %d\n", p->process_id, p->group->group_id, time_passed);
		mh_print(p->group->mh);
	}

	yieldL(p, time_passed);
	assert(p->group->nthread >= p->group->nqueued);
	p->group->nthread -= 1;

	if (grp_is_sleep(p->group)) {
		proc_lag_vruntime(p, mh_min(lh));
		ticks_gettime(p->group->sleepstart);
	}
	pthread_rwlock_unlock(&p->proc_lock);
	lh_unlock(lh);
}
