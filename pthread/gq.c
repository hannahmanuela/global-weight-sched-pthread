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

static void enq_proc_vt(struct task_struct *p) {
	p->he.vruntime = safe_read_tsc();
	queue_t *q = &ss_global->q_h;
	if (p->he.weight == W_LOW) {
		q = &ss_global->q_l;
	}
	
	while(1) {
		if(queue_push(q, p))
			break;
	}
	// assert(ok);
}

// Select next process to run
struct task_struct *ss_schedule_gq(struct task_struct *prev) {
	if(prev) {
		if(debug) {
			printf("%d(%d): yield_gq \n", prev->pid, prev->group->gid);
		}
	}
	struct task_struct *p = queue_pop(&ss_global->q_h);
	if (p != NULL) {
		if(prev != NULL) {
			enq_proc_vt(prev);
		}
		goto ok;
	}
	if (prev != NULL && prev->he.weight == W_HIGH) {
		p = prev;
		mycore()->nlocal += 1;
		goto ok;
	}

	mycore()->nskip_high++;
	if ((p = queue_pop(&ss_global->q_l)) != NULL) {
		if(prev != NULL) {
			enq_proc_vt(prev);
		}
		goto ok;
	} else if (prev != NULL) {
		p = prev;
		mycore()->nlocal += 1;
		goto ok;
	}
	mycore()->nsched_null += 1;
	return NULL;
ok:
	if(debug) {
		printf("%d: schedule_gq %d(%d) vt %lld\n", mycore()->cid, p->pid, p->group->gid, p->he.vruntime);
	}
	if(mycore()->fd > 0) {
		c_log_append(p);
	}
	return p;
}

// Enqueue p at the ends of its group's queue
void ss_enqueue_gq(struct task_struct *p) {
	enq_proc_vt(p);

	if(debug) {
		printf("%d(%d): enqueue_gq\n", p->pid, p->group->gid);
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
