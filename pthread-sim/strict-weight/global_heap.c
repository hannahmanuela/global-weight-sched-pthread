#include <assert.h>
#include <limits.h>
#include <stdio.h>

#include "driver.h"
#include "lheap.h"
#include "group.h"
#include "mheap.h"

extern int tick_length;

// Select next process to run
struct process *schedule(struct mheap *mh) {
	struct group *min_group = mh_min_group(mh);
	if (min_group == NULL) {
		return NULL;
	}

        // gl_min_group returns with heap and group lock held
    
	int svt_inc = vt_inc(tick_length, mh->tot_weight, min_group->weight);

	printf("%d: schedule: svt_inc %d\n", min_group->group_id, svt_inc);
	mh_print(min_group->mh);

	grp_upd_spec_virt_time_avg(min_group, svt_inc);

	// select the next process
	struct process *next_p = grp_deq_process(min_group);

	next_p->tot_weight = mh->tot_weight;

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
	printf("%d: enqueue %d is_unrunnable %d\n", p->group->group_id, is_unrunnable);
	mh_print(p->group->mh);
	grp_add_process(p);
	if (is_unrunnable) {
		ticks_gettime(p->group->time);
		ticks_sub(p->group->time, p->group->sleepstart);
		ticks_add(p->group->sleeptime, p->group->time);
		grp_set_init_spec_virt_time(p->group, lh_avg_spec_virt_time_inc(p->group->lh)); 
		heap_fix_index(p->group->lh->heap, &p->group->heap_elem);
	}
	pthread_rwlock_unlock(&p->group->group_lock);
	lh_unlock(p->group->lh);
}

// Process p yields core
static int yieldL(struct process *p, int time_passed) {
	p->group->runtime += time_passed;
	return grp_adjust_spec_virt_time(p, time_passed, tick_length);
}

// Yield and enqueue
void yield(struct process *p, int time_passed) {
	lh_lock_timed(p->group->lh);
	pthread_rwlock_wrlock(&p->group->group_lock);

	printf("%d: yield time_passed %d\n", p->group->group_id, time_passed);
	mh_print(p->group->mh);

	int vt_inc = yieldL(p, time_passed);
	bool is_unrunnable = p->group->threads_queued == 0;
	grp_add_process(p);
	if(vt_inc > 0 || is_unrunnable)
		heap_fix_index(p->group->lh->heap, &p->group->heap_elem);
	pthread_rwlock_unlock(&p->group->group_lock);
	lh_unlock(p->group->lh);
}

// Process p is not runnable and yields core, which may make
// p's group not runnable
void dequeue(struct process *p, int time_passed) {
	lh_lock_timed(p->group->lh);
	pthread_rwlock_wrlock(&p->group->group_lock);

	printf("%d: dequeue %d\n", p->group->group_id, time_passed);
	mh_print(p->group->mh);

	p->group->num_threads -= 1;
	assert(p->group->num_threads >= p->group->threads_queued);
	int vt_inc = yieldL(p, time_passed);
	bool is_unrunnable = p->group->threads_queued == 0;
	if (is_unrunnable) {
		grp_store_spec_virt_time_inc(p->group, vt_inc);
		ticks_gettime(p->group->sleepstart);
	}
	if (vt_inc > 0 || is_unrunnable) {
		heap_fix_index(p->group->lh->heap, &p->group->heap_elem);
	}
	pthread_rwlock_unlock(&p->group->group_lock);
	lh_unlock(p->group->lh);
}
