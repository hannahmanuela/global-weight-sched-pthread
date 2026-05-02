#include "core.h"

bool ss_schedule_gq(struct sched_state *ss, struct core *c);
void ss_yield_gq(struct sched_state *ss, struct core *c, struct process *p, t_t time_passed);
void ss_enqueue_gq(struct sched_state *ss, struct core *c, struct process *p);
void ss_dequeue_gq(struct sched_state *ss, struct core *c, struct process *p, t_t time_gotten);
