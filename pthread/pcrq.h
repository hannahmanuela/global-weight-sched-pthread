#include "core.h"
#include "sched_state.h"

struct task_struct *ss_schedule_pcrq(struct task_struct *prev);
void ss_yield_pcrq(struct task_struct *p, t_t time_passed);
void ss_enqueue_pcrq(struct task_struct *p);
void ss_dequeue_pcrq(struct task_struct *p, t_t time_gotten);
