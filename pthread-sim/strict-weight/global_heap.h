#include "group.h"

struct process *schedule(int core, struct mheap *mh);
void yield(struct process *p, t_t time_passed);
void enqueue(struct process *p);
void dequeue(struct process *p, t_t time_gotten);
