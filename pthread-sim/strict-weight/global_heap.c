#include <assert.h>
#include <limits.h>

#include "lheap.h"
#include "group.h"
#include "group_list.h"

// Make p runnable.
// caller should have no locks
void enqueue(struct group_list *gl, struct process *p, int is_new) {
	pthread_rwlock_wrlock(&p->group->group_lock);
	bool was_empty = p->group->threads_queued == 0;
	grp_add_process(p, is_new); 
	if (is_new) {
		p->group->num_threads += 1;
	}
	p->group->threads_queued += 1;
	pthread_rwlock_unlock(&p->group->group_lock);
	if (was_empty) {
		grp_set_spec_virt_time(p->group, gl_avg_spec_virt_time(p->group)); 
	}
}


// Assumes caller holds p's group lock
// returns with no locks
static void dequeue(struct group_list *gl, struct process *p) {

	grp_del_process(p);

	bool now_empty = p->group->threads_queued == 0;

	pthread_rwlock_unlock(&p->group->group_lock);
    
	if (!now_empty) {
		return;
	}

	// there's a potential race with enq here
	// enq will have set the threads_queued to 1, so re-check before setting
	// unlocking the group before getting gl_avg_spec_virt_time is required because of the lock order
	int curr_avg_spec_virt_time = gl_avg_spec_virt_time(p->group);

	pthread_rwlock_wrlock(&p->group->group_lock);
	if (p->group->threads_queued > 0) { // someone else enq'd while we were deq'ing, don't overwrite the virt time
		pthread_rwlock_unlock(&p->group->group_lock);
		return;
	}
	int spec_virt_time = p->group->spec_virt_time;
	p->group->virt_lag = curr_avg_spec_virt_time - spec_virt_time;
	p->group->last_virt_time = spec_virt_time;
	pthread_rwlock_unlock(&p->group->group_lock);
}

struct process *schedule(struct process *running_process, struct group_list *gl, int time_passed, int should_re_enq, int tick_length) {
	struct group *prev_running_group = NULL;

	if (running_process) {
		if (grp_adjust_spec_virt_time(running_process->group, time_passed, tick_length)) {
			gl_fix_group(running_process->group);
		}
	}

	if (should_re_enq && running_process) {
		enqueue(gl, running_process, 0);
	}

	struct group *min_group = gl_min_group(gl); // returns with both locks held
	if (min_group == NULL) {
		return NULL;
	}
    
	int time_expecting = (int)tick_length / min_group->weight;
	gl_update_group_svt(min_group, time_expecting);
	min_group->threads_queued -= 1;

	assert(min_group->threads_queued >= 0);

	lh_unlock(min_group->lh);

	// select the next process
	struct process *next_p = min_group->runqueue_head;
	dequeue(gl, next_p); // unlocks the group lock
    
	next_p->next = NULL;
	return next_p;
}

// NOTE: assume we hold no locks
void yield(struct group_list *gl, struct process *p, int time_gotten, int tick_length) {
	grp_dec_nthread(p->group);
	schedule(p, gl, time_gotten, 0, tick_length);
}


