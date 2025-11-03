#include "group.h"
#ifndef GLOBAL_HEAP_H
#define GLOBAL_HEAP_H
struct process *schedule(int core, struct mheap *mh);
void yield(struct process *p, t_t time_passed);
void enqueue(struct process *p);
void dequeue(struct process *p, t_t time_gotten);
#endif
