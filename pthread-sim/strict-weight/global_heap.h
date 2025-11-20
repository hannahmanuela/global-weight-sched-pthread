#include "core.h"
#include "group.h"

struct global_heap {
	int tick_length;
	struct mheap *mh;
};

struct global_heap *gh_new(int tick_length, int cmp(struct heap_elem *, struct heap_elem *), int n);
struct process *schedule(struct global_heap *gh, struct core *c);
void yield(struct global_heap *gh, struct core *c, struct process *p, t_t time_passed);
void enqueue(struct global_heap *gh, struct core *c, struct process *p);
void dequeue(struct global_heap *gh, struct core *c, struct process *p, t_t time_gotten);
void stats(struct global_heap *gh, struct group *gs[], int n);
void print(struct global_heap *gh, struct group *gs[], int n);
