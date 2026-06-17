#ifndef _SCHED_STATE_H_

#define _SCHED_STATE_H_

#include "core.h"
#include "group.h"
#include "mpmcv1.h"
#include "preempt.h"
#include "dllist.h"

#define GWFS 1
#define RR   2
#define PCRQ 3
#define GQ 4

struct sched_state;

struct scheduler {
	struct task_struct *(*schedule)(struct task_struct *prev);
	void (*yield)(struct task_struct *p, t_t time_passed);
	void (*enqueue)(struct task_struct *p);
	void (*dequeue)(struct task_struct *p, t_t time_gotten);
};

struct sched_state {
	struct scheduler sched;
	struct core **cs;
	int ncore;
	int tick_length;

	// XXX group array and stick these fields inside of group
	struct mheap *mh;
	struct mheap *mh_l;   // for low priority rr procs

	queue_t q_h __calign__;
	queue_t q_l __calign__;

	dllist_t preemptq __calign__;

	preempt_t preempt __calign__;
	struct mheap *mh_r __calign__;

	bitarray_t preemptable __calign__;

	vt_t min_vt __calign__;
};

struct sched_state *ss_new(int tick_length, int n, struct core *cs[], int ncore);
struct core *ss_choose_core(struct sched_state *ss, struct core *c);
void ss_stats(struct sched_state *ss, struct group *gs[], int n);
void ss_print(struct sched_state *ss, struct group *gs[], int n);


void set_scheduler(char *s);

bool is_rr();
bool is_pcrq();
bool is_gq();

bool ss_schedule(struct sched_state *ss, struct core *c);
void ss_yield(struct sched_state *ss, struct core *c, struct task_struct *p, t_t time_passed);
void ss_enqueue(struct sched_state *ss, struct core *c, struct task_struct *p);
void ss_dequeue(struct sched_state *ss, struct core *c, struct task_struct *p, t_t time_gotten);

#endif
