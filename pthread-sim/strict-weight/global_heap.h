#include "group.h"

struct process *schedule(int core, struct mheap *mh, long *ts, long *retry);
void yield(int c, struct process *p, t_t time_passed, long *retry);
void enqueue(int c, struct process *p, long *retry);
void dequeue(struct process *p, t_t time_gotten);
void stats(struct group *gs[], int n);
