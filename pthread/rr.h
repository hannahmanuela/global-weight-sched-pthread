#include "core.h"

#define RR_HIGH 0
#define RR_LOW 1

bool ss_schedule_rr(struct sched_state *ss, struct core *c);
void ss_yield_rr(struct sched_state *ss, struct core *c, struct process *p, t_t time_passed);
void ss_enqueue_rr(struct sched_state *ss, struct core *c, struct process *p);
void ss_dequeue_rr(struct sched_state *ss, struct core *c, struct process *p, t_t time_gotten);
