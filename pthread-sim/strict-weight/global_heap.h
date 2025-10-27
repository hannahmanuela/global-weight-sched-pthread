#include "group.h"
#include "group_list.h"

struct process *schedule(struct group_list *gl);
void yield(struct process *p, int time_passed);
void enqueue(struct process *p);
void dequeue(struct process *p, int time_gotten);
