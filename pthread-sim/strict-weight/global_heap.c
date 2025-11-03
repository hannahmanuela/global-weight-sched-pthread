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
	struct group *min_group = mh_min_group(mh);
	if (min_group == NULL) {
		return NULL;
	}

        // gl_min_group returns with heap and group lock held
    
	if(debug) {
		printf("%d(%d): schedule\n", min_group->group_id, core);
		mh_print(min_group->mh);
	}

	grp_upd_vruntime(min_group, mh->tick_length);

	// select the next process
	struct process *next_p = grp_deq_process(min_group);
	assert(next_p != NULL);
	next_p->group->nrunning += 1;
	
	// must be after grp_deq_process, since it may empty the proc queue
	heap_fix_index(min_group->lh->heap, &min_group->heap_elem);

	pthread_rwlock_unlock(&next_p->group->group_lock);
	lh_unlock(min_group->lh);

	return next_p;
}

// Make p runnable, which may make the group runnable.
void enqueue(struct process *p) {
	pthread_rwlock_wrlock(&p->group->group_lock);
	p->group->nthread += 1;
	bool none_queued = p->group->nqueued == 0;
	bool was_sleep = grp_is_sleep(p->group);
		 
	if(debug) {
		printf("%d(%d): enqueue was_sleep %d lh%p\n", p->group->group_id, p->core_id, was_sleep, p->group->lh);
		mh_print(p->group->mh);
	}

	grp_add_process(p);
	if(none_queued && !was_sleep)
		heap_fix_index(p->group->lh->heap, &p->group->heap_elem);

	pthread_rwlock_unlock(&p->group->group_lock);

	if (was_sleep) {
		grp_enqueue(p->group);
	} 
}

// Process p yields core
static bool yieldL(struct process *p, int time_passed) {
	p->group->runtime += time_passed;
	p->group->nrunning -= 1;
	return grp_adjust_vruntime(p->group, time_passed, p->group->mh->tick_length);
}

// Yield and enqueue
void yield(struct process *p, t_t time_passed) {
	lh_lock_timed(p->group->lh);
	pthread_rwlock_wrlock(&p->group->group_lock);

	if(debug) {
		printf("%d(%d): yield time_passed %ld\n", p->group->group_id, p->core_id, time_passed);
		mh_print(p->group->mh);
	}
	bool none_queued = p->group->nqueued == 0;
	bool fix_heap = yieldL(p, time_passed);
	grp_add_process(p);   // now group has procs queued; fix heap
	if(none_queued || fix_heap)
		heap_fix_index(p->group->lh->heap, &p->group->heap_elem);
	pthread_rwlock_unlock(&p->group->group_lock);
	lh_unlock(p->group->lh);
}

// Process p is not runnable and yields core, which may make
// p's group not runnable
void dequeue(struct process *p, t_t time_passed) {
	struct lock_heap *lh = p->group->lh;
	lh_lock_timed(lh);
	pthread_rwlock_wrlock(&p->group->group_lock);

	if(debug) {
		printf("%d(%d): dequeue %ld\n", p->group->group_id, p->core_id, time_passed);
		mh_print(p->group->mh);
	}

	p->group->nthread -= 1;
	assert(p->group->nthread >= p->group->nqueued);
	bool fix_heap = yieldL(p, time_passed);
	bool is_sleep = grp_is_sleep(p->group);
	if (fix_heap) {
		heap_fix_index(lh->heap, &p->group->heap_elem);
	}
	if (is_sleep) {
		grp_lag_vruntime(p->group, mh_min(lh));
		mh_del_group(p->group->mh, p->group);
		ticks_gettime(p->group->sleepstart);
	}
	pthread_rwlock_unlock(&p->group->group_lock);
	lh_unlock(lh);
}
