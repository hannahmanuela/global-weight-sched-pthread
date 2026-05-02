#include "core.h"
#include "sched_state.h"

bool ss_schedule_pcrq(struct sched_state *ss, struct core *c);
void ss_yield_pcrq(struct sched_state *ss, struct core *c, struct process *p, t_t time_passed);
void ss_enqueue_pcrq(struct sched_state *ss, struct core *c, struct process *p);
void ss_dequeue_pcrq(struct sched_state *ss, struct core *c, struct process *p, t_t time_gotten);
