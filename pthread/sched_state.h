#ifndef _SCHED_STATE_H_

#define _SCHED_STATE_H_

#include "vt.h"
#include "core.h"
#include "group.h"
#include "mpmcv1.h"
#include "preempt.h"
#include "dllist.h"

#define GWFS 1
#define RR   2
#define PCRQ 3
#define GQ 4
#define RR1  5
#define GPPCRQ  6

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

	struct mheap *mh;

	// for rr1.c
	struct mheap *mh_r __calign__;   // for running low priority procs w runq
	bitarray_t preemptable __calign__;  // for running low priority procs w mask

	// for gq.c
	queue_t q_h __calign__;
	queue_t q_l __calign__;

	// for dllist
	dllist_t preemptq __calign__;

	// for gwfs; TODO: convert to rr1 plan
	preempt_t preempt __calign__;

	// for gwfs.c
	vt_t min_vt __calign__;

	// for rr; TODO delete rr
	struct mheap *mh_l;   // for low priority rr procs

	is_lt_elem_t is_lt_elem;
	is_min_elem_t is_min_elem;
};

struct sched_state *ss_new(int tick_length, int n, struct core *cs[], int ncore, is_lt_elem_t lt, is_min_elem_t min);
struct core *ss_choose_core(struct sched_state *ss, struct core *c);
void ss_stats(struct sched_state *ss, struct group *gs[], int n);
void ss_print(struct sched_state *ss, struct group *gs[], int n);


void set_scheduler(char *s);

bool is_rr();
bool is_pcrq();
bool is_gppcrq();
bool is_gq();
bool is_gwfs();
bool is_rr1();

bool ss_schedule(struct sched_state *ss, struct core *c);
void ss_yield(struct sched_state *ss, struct core *c, struct task_struct *p, t_t time_passed);
void ss_enqueue(struct sched_state *ss, struct core *c, struct task_struct *p);
void ss_dequeue(struct sched_state *ss, struct core *c, struct task_struct *p, t_t time_gotten);

#endif
