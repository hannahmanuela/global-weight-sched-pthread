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
    
	int svt_inc = vt_inc(tick_length, gl->mh->tot_weight, min_group->weight);
	printf("%d: schedule: svt_inc %d\n", min_group->group_id, svt_inc);
	grp_upd_spec_virt_time_avg(min_group, svt_inc);

	// select the next process
	struct process *next_p = grp_deq_process(min_group);

	next_p->tot_weight = gl->mh->tot_weight;

	// must be after grp_deq_process, since it may empty the proc queue
	heap_fix_index(min_group->lh->heap, &min_group->heap_elem);

	pthread_rwlock_unlock(&next_p->group->group_lock);
	lh_unlock(min_group->lh);

	return next_p;
}

void enqueue(struct process *p) {
	lh_lock_timed(p->group->lh);
	pthread_rwlock_wrlock(&p->group->group_lock);
	printf("enqueue %d\n", p->group->group_id);
	p->group->num_threads += 1;
	bool was_empty = p->group->threads_queued == 0;
	grp_add_process(p);
	if (was_empty) {
		ticks_gettime(p->group->time);
		long t = p->group->time[p->core_id] - p->group->sleepstart[p->core_id];
		p->group->sleeptime += t; 
		printf("enqueueL: %d(%d) sleep time %d\n", p->group->group_id, p->core_id, t);
		grp_set_init_spec_virt_time(p->group, gl_avg_spec_virt_time_inc(p->group->lh)); 
		heap_fix_index(p->group->lh->heap, &p->group->heap_elem);
	}
	pthread_rwlock_unlock(&p->group->group_lock);
	lh_unlock(p->group->lh);
}

// process p yields core
static int yieldL(struct process *p, int time_passed) {
	p->group->runtime += time_passed;
	return grp_adjust_spec_virt_time(p, time_passed, tick_length);
}

// yield and enqueue
void yield(struct process *p, int time_passed) {
	lh_lock_timed(p->group->lh);
	pthread_rwlock_wrlock(&p->group->group_lock);
	printf("yield %d\n", p->group->group_id);
	int vt_inc = yieldL(p, time_passed);
	bool is_unrunnable = p->group->threads_queued == 0;
	grp_add_process(p);
	if(vt_inc > 0 || is_unrunnable)
		heap_fix_index(p->group->lh->heap, &p->group->heap_elem);
	pthread_rwlock_unlock(&p->group->group_lock);
	lh_unlock(p->group->lh);
}

// process p is not runnable and yields core
void dequeue(struct process *p, int time_passed) {
	lh_lock_timed(p->group->lh);
	pthread_rwlock_wrlock(&p->group->group_lock);
	printf("%d: dequeue %d\n", p->group->group_id, time_passed);
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
