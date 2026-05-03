#ifndef _SCHEDULER_H_

#define _SCHEDULER_H_

#include <string.h>

#include "core.h"
#include "sched_state.h"

#define GWFS 1
#define RR   2
#define PCRQ 3
#define GQ 4

void set_scheduler(char *s);

bool is_rr();
bool is_pcrq();
bool is_gq();

bool ss_schedule(struct sched_state *ss, struct core *c);
void ss_yield(struct sched_state *ss, struct core *c, struct process *p, t_t time_passed);
void ss_enqueue(struct sched_state *ss, struct core *c, struct process *p);
void ss_dequeue(struct sched_state *ss, struct core *c, struct process *p, t_t time_gotten);

#endif
