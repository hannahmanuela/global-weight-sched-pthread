#include <assert.h>
#include <limits.h>
#include <stdio.h>

#include "driver.h"
#include "lheap.h"
#include "group.h"
#include "mheap.h"

extern int tick_length;
extern bool debug;

// Select next process to run
struct process *schedule(struct mheap *mh) {
	struct group *min_group = mh_min_group(mh);
	if (min_group == NULL) {
		return NULL;
	}

        // gl_min_group returns with heap and group lock held
    
	if(debug) {
		printf("%d: schedule\n", min_group->group_id);
		mh_print(min_group->mh);
	}

	grp_upd_vruntime(min_group, tick_length);

	// select the next process
	struct process *next_p = grp_deq_process(min_group);

	// must be after grp_deq_process, since it may empty the proc queue
	heap_fix_index(min_group->lh->heap, &min_group->heap_elem);

	pthread_rwlock_unlock(&next_p->group->group_lock);
	lh_unlock(min_group->lh);

	return next_p;
}

// Make p runnable, which may make the group runnable.
void enqueue(struct process *p) {
	lh_lock_timed(p->group->lh);
	pthread_rwlock_wrlock(&p->group->group_lock);
	p->group->num_threads += 1;
	bool is_unrunnable = p->group->threads_queued == 0;

	if(debug) {
		printf("%d: enqueue is_unrunnable %d\n", p->group->group_id, is_unrunnable);
		mh_print(p->group->mh);
	}

	grp_add_process(p);
	if (is_unrunnable) {
		ticks_gettime(p->group->time);
		ticks_sub(p->group->time, p->group->sleepstart);
		printf("%d: sleep time %d\n", p->group->group_id, ticks_sum(p->group->time));
		ticks_add(p->group->sleeptime, p->group->time);
                grp_set_init_vruntime(p->group, mh_min(p->group->lh));
		heap_fix_index(p->group->lh->heap, &p->group->heap_elem);
	}
	pthread_rwlock_unlock(&p->group->group_lock);
	lh_unlock(p->group->lh);
}

// Process p yields core
static bool yieldL(struct process *p, int time_passed) {
	p->group->runtime += time_passed;
	return grp_adjust_vruntime(p->group, time_passed, tick_length);
}

// Yield and enqueue
void yield(struct process *p, int time_passed) {
	lh_lock_timed(p->group->lh);
	pthread_rwlock_wrlock(&p->group->group_lock);

	if(debug) {
		printf("%d: yield time_passed %d\n", p->group->group_id, time_passed);
		mh_print(p->group->mh);
	}

	bool fix_heap = yieldL(p, time_passed);
	bool is_unrunnable = p->group->threads_queued == 0;
	grp_add_process(p);
	if(fix_heap || is_unrunnable)
		heap_fix_index(p->group->lh->heap, &p->group->heap_elem);
	pthread_rwlock_unlock(&p->group->group_lock);
	lh_unlock(p->group->lh);
}

// Process p is not runnable and yields core, which may make
// p's group not runnable
void dequeue(struct process *p, int time_passed) {
	lh_lock_timed(p->group->lh);
	pthread_rwlock_wrlock(&p->group->group_lock);

	if(debug) {
		printf("%d: dequeue %d\n", p->group->group_id, time_passed);
		mh_print(p->group->mh);
	}

	p->group->num_threads -= 1;
	assert(p->group->num_threads >= p->group->threads_queued);
	bool fix_heap = yieldL(p, time_passed);
	bool is_unrunnable = p->group->threads_queued == 0;
	if (fix_heap || is_unrunnable) {
		heap_fix_index(p->group->lh->heap, &p->group->heap_elem);
	}
	if (is_unrunnable) {
		grp_lag_vruntime(p->group, mh_min(p->group->lh));
		ticks_gettime(p->group->sleepstart);
	}
	pthread_rwlock_unlock(&p->group->group_lock);
	lh_unlock(p->group->lh);
}
