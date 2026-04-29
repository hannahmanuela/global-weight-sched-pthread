#include "core.h"
#include "group.h"
#include "mpmc.h"

struct global_heap {
	struct core **cs;
	int ncore;
	int tick_length;

	// XXX group array and stick these fields inside of group
	struct mheap *mh;
	struct mheap *mh1;   // for low priority rr procs

	queue_t q;

	preempt_t preempt __calign__;
};

struct global_heap *gh_new(int tick_length, int n, struct core *cs[], int ncore);
bool gh_schedule(struct global_heap *gh, struct core *c);
void gh_yield(struct global_heap *gh, struct core *c, struct process *p, t_t time_passed);
void gh_enqueue(struct global_heap *gh, struct core *c, struct process *p);
void gh_dequeue(struct global_heap *gh, struct core *c, struct process *p, t_t time_gotten);
void gh_stats(struct global_heap *gh, struct group *gs[], int n);
void gh_print(struct global_heap *gh, struct group *gs[], int n);
