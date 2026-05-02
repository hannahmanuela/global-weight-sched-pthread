#include "core.h"
#include "group.h"
#include "mpmc.h"

struct sched_state {
	struct core **cs;
	int ncore;
	int tick_length;

	// XXX group array and stick these fields inside of group
	struct mheap *mh;
	struct mheap *mh1;   // for low priority rr procs

	queue_t q;

	preempt_t preempt __calign__;
};

struct sched_state *ss_new(int tick_length, int n, struct core *cs[], int ncore);
struct core *ss_choose_core(struct sched_state *ss, struct core *c);
void ss_stats(struct sched_state *ss, struct group *gs[], int n);
void ss_print(struct sched_state *ss, struct group *gs[], int n);
