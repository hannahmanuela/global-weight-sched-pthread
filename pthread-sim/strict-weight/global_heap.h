#include "core.h"
#include "group.h"

struct process *schedule(struct core *c, struct mheap *mh);
void yield(struct core *c, struct process *p, t_t time_passed);
void enqueue(struct core *c, struct process *p);
void dequeue(struct process *p, t_t time_gotten);
void stats(struct group *gs[], int n);
