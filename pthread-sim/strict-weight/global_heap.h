#include "core.h"
#include "group.h"

struct global_heap {
	int tick_length;
};

struct global_heap *gh_new(int tick_length);
struct process *schedule(struct global_heap *gh, struct core *c, struct mheap *mh);
void yield(struct global_heap *gh, struct core *c, struct process *p, t_t time_passed);
void enqueue(struct global_heap *gh, struct core *c, struct process *p);
void dequeue(struct global_heap *gh, struct core *c, struct process *p, t_t time_gotten);
void stats(struct global_heap *gh, struct group *gs[], int n);
void print(struct global_heap *gh, struct mheap *mh, struct group *gs[], int n);
