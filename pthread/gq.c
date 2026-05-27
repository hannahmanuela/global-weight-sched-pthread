#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>

#include "vt.h"
#include "util.h"
#include "driver.h"
#include "sched_state.h"
#include "core.h"
#include "mpmcv1.h"
#include "rr.h"

//
// rr with global mpmc queue
//

extern bool debug;
extern struct sched_state *ss_global;

// Select next process to run from mh
struct task_struct *ss_schedule_q(queue_t *q) {
	struct task_struct *min_proc = NULL;

	min_proc = queue_pop(q);
	if (min_proc == NULL) {
		mycore()->process = NULL;
		mycore()->nsched_null += 1;
		return NULL;
	}

	if(debug) {
		printf("%d: schedule_gq %d(%d) vt %lld\n", mycore()->cid, min_proc->pid, min_proc->group->gid, min_proc->he.vruntime);
	}
	if(mycore()->fd > 0) {
		c_log_append(min_proc);
	}
	return min_proc;
}

static void enq_proc_vt(struct task_struct *p) {
	p->he.vruntime = safe_read_tsc();
	while(1) {
		if(queue_push(&ss_global->q, p))
			break;
	}
	// assert(ok);
	mycore()->process = NULL;
}

// Select next process to run
struct task_struct *ss_schedule_gq(struct task_struct *prev) {
	if(prev) {
		if(debug) {
			printf("%d(%d): yield_gq \n", prev->pid, prev->group->gid);
		}
		enq_proc_vt(prev);
	}
	return ss_schedule_q(&ss_global->q);
}

// Enqueue p at the ends of its group's queue
void ss_enqueue_gq(struct task_struct *p) {
	enq_proc_vt(p);

	if(debug) {
		printf("%d(%d): enqueue_gq %p\n", p->pid, p->group->gid, p->group->mh);
	}
}

// Process p yields after it ran for a tick, append it to the end of its queue
void ss_yield_gq(struct task_struct *p, t_t time_passed) {
	p->runtime += time_passed;

}

// Process p is not runnable and yields core
void ss_dequeue_gq(struct task_struct *p, t_t time_passed) {
	p->runtime += time_passed;
	if(debug) {
		printf("%d(%d): dequeue_gq %ld\n", p->pid, p->group->gid, time_passed);
	}
	p->h = NULL;
}
