#include <assert.h>
#include <limits.h>

#include "lheap.h"
#include "group.h"
#include "group_list.h"

// select next process to run
struct process *schedule(struct group_list *gl, int tick_length) {
	struct group *min_group = gl_min_group(gl);
	if (min_group == NULL) {
		return NULL;
	}

        // gl_min_group returns with both locks held
    
	int time_expecting = (int)tick_length / min_group->weight;
	gl_update_group_svt(min_group, time_expecting);
	min_group->threads_queued -= 1;

	assert(min_group->threads_queued >= 0);

	// select the next process
	struct process *next_p = min_group->runqueue_head;
	grp_set_new_spec_virt_time(next_p, gl_avg_spec_virt_time(next_p->group));
    
	pthread_rwlock_unlock(&next_p->group->group_lock);
	lh_unlock(min_group->lh);

	next_p->next = NULL;
	return next_p;
}

// Make p runnable.
// caller must hold heap and group lock
static void enqueueL(struct process *p, int is_new) {
	bool was_empty = p->group->threads_queued == 0;
	grp_add_process(p, is_new); 
	if (is_new) {
		p->group->num_threads += 1;
	}
	p->group->threads_queued += 1;
	if (was_empty) {
		grp_set_init_spec_virt_time(p->group, gl_avg_spec_virt_time(p->group)); 
	}
}

void enqueue(struct process *p, int is_new) {
	lh_lock_timed(p->group->lh);
	pthread_rwlock_wrlock(&p->group->group_lock);
	enqueueL(p, is_new);
	pthread_rwlock_unlock(&p->group->group_lock);
	lh_unlock(p->group->lh);
}

// process p yields core
void yield(struct process *p, int time_passed, int should_re_enq, int tick_length) {
	lh_lock_timed(p->group->lh);
	pthread_rwlock_wrlock(&p->group->group_lock);
	if (grp_adjust_spec_virt_time(p->group, time_passed, tick_length)) {
		gl_fix_group(p->group);
	}
	if (should_re_enq) {
		enqueueL(p, 0);
	}
	pthread_rwlock_unlock(&p->group->group_lock);
	lh_unlock(p->group->lh);
}

// process p is not runnable and yields core
void dequeue(struct process *p, int time_gotten, int tick_length) {
	grp_dec_nthread(p->group);
	yield(p, time_gotten, 0, tick_length);
}
