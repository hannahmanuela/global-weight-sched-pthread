#ifndef _SCHED_STATE_H_

#define _SCHED_STATE_H_

#include "core.h"
#include "group.h"
#include "mpmcv1.h"
#include "preempt.h"
#include "dllist.h"

struct sched_state;

struct scheduler {
	bool (*schedule)(struct sched_state *ss, struct core *c);
	void (*yield)(struct sched_state *ss, struct core *c, struct task_struct *p, t_t time_passed);
	void (*enqueue)(struct sched_state *ss, struct core *c, struct task_struct *p);
	void (*dequeue)(struct sched_state *ss, struct core *c, struct task_struct *p, t_t time_gotten);
};

struct schedulerv1 {
	struct task_struct *(*schedule)(struct task_struct *prev);
	void (*yield)(struct task_struct *p, t_t time_passed);
	void (*enqueue)(struct task_struct *p);
	void (*dequeue)(struct task_struct *p, t_t time_gotten);
};

struct sched_state {
	struct scheduler sched;
	struct schedulerv1 schedv1;
	struct core **cs;
	int ncore;
	int tick_length;

	// XXX group array and stick these fields inside of group
	struct mheap *mh;
	struct mheap *mh1;   // for low priority rr procs

	queue_t q __calign__;

	dllist_t preemptq __calign__;

	preempt_t preempt __calign__;

	bitarray_t preemptable __calign__;

	vt_t min_vt __calign__;
};

struct sched_state *ss_new(int tick_length, int n, struct core *cs[], int ncore);
struct core *ss_choose_core(struct sched_state *ss, struct core *c);
void ss_stats(struct sched_state *ss, struct group *gs[], int n);
void ss_print(struct sched_state *ss, struct group *gs[], int n);

#endif
