#ifndef _SCHEDULER_H_

#include <string.h>

#include "core.h"
#include "global_heap.h"

#define _SCHEDULER_H_

#define GWFS 1
#define RR   2
#define PCRQ 3

void set_scheduler(char *s);

bool is_rr();

bool gh_schedule(struct global_heap *gh, struct core *c);
void gh_yield(struct global_heap *gh, struct core *c, struct process *p, t_t time_passed);
void gh_enqueue(struct global_heap *gh, struct core *c, struct process *p);
void gh_dequeue(struct global_heap *gh, struct core *c, struct process *p, t_t time_gotten);

#endif
