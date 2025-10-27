#include <assert.h>
#include <limits.h>
#include <stdio.h>

#include "driver.h"
#include "lheap.h"
#include "group.h"
#include "group_list.h"

extern int tick_length;

// select next process to run
struct process *schedule(struct group_list *gl) {
	struct group *min_group = gl_min_group(gl);
	if (min_group == NULL) {
		return NULL;
	}

        // gl_min_group returns with heap and group lock held
    
	int svt_inc = (int)tick_length / min_group->weight;
	grp_upd_spec_virt_time_avg(min_group, svt_inc);
	min_group->threads_queued -= 1;
	heap_fix_index(min_group->lh->heap, &min_group->heap_elem);

	assert(min_group->threads_queued >= 0);

	// select the next process
	struct process *next_p = grp_deq_process(min_group);

	// grp_lag_spec_virt_time(next_p, gl_avg_spec_virt_time_inc(min_group));
    
	pthread_rwlock_unlock(&next_p->group->group_lock);
	lh_unlock(min_group->lh);

	return next_p;
}

// Make p runnable.
// caller must hold heap and group lock
static void enqueueL(struct process *p, int is_new) {
	bool was_empty = p->group->threads_queued == 0;
	grp_add_process(p);
	p->group->threads_queued += 1;
	if (was_empty && is_new) {
		ticks_gettime(p->group->time);
		long t = p->group->time[p->core_id] - p->group->sleepstart[p->core_id];
		p->group->sleeptime += t; 
		printf("enqueueL: %d(%d) sleep time %d\n", p->group->group_id, p->core_id, t);
		grp_set_init_spec_virt_time(p->group, gl_avg_spec_virt_time_inc(p->group)); 
		heap_fix_index(p->group->lh->heap, &p->group->heap_elem);
	} else if (was_empty) {
		heap_fix_index(p->group->lh->heap, &p->group->heap_elem);
	}
}

void enqueue(struct process *p) {
	lh_lock_timed(p->group->lh);
	pthread_rwlock_wrlock(&p->group->group_lock);
	p->group->num_threads += 1;
        enqueueL(p, 1);
	pthread_rwlock_unlock(&p->group->group_lock);
	lh_unlock(p->group->lh);
}

// process p yields core
static void yieldL(struct process *p, int time_passed) {
	p->group->runtime += time_passed;
	int fix_heap = grp_adjust_spec_virt_time(p->group, time_passed, tick_length);
	// XXX if group queue becomes > 0 and we yielded early, then
	// we do two heap_fix_index
	if(fix_heap)
		heap_fix_index(p->group->lh->heap, &p->group->heap_elem);
}

// yield and enqueue
void yield(struct process *p, int time_passed) {
	lh_lock_timed(p->group->lh);
	pthread_rwlock_wrlock(&p->group->group_lock);
	yieldL(p, time_passed);
	enqueueL(p, 0);
	pthread_rwlock_unlock(&p->group->group_lock);
	lh_unlock(p->group->lh);
}

// process p is not runnable and yields core
void dequeue(struct process *p, int time_passed) {
	lh_lock_timed(p->group->lh);
	pthread_rwlock_wrlock(&p->group->group_lock);
	p->group->num_threads -= 1;
	if (p->group->threads_queued == 0)
		ticks_gettime(p->group->sleepstart);
	assert(p->group->num_threads >= p->group->threads_queued);
	yieldL(p, time_passed);
	pthread_rwlock_unlock(&p->group->group_lock);
	lh_unlock(p->group->lh);
}
