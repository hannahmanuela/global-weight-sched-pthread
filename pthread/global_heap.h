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
struct core *gh_choose_core(struct global_heap *gh, struct core *c);
void gh_stats(struct global_heap *gh, struct group *gs[], int n);
void gh_print(struct global_heap *gh, struct group *gs[], int n);
