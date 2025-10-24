#include "group.h"
#include "group_list.h"

struct process *schedule(struct group_list *gl, int tick_length);
void yield(struct process *p, int time_passed, int re_enq, int tick_length);
void enqueue(struct process *p, int is_new);
void dequeue(struct process *p, int time_gotten, int tick_length);
