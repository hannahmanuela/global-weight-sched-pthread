#include "core.h"
#include "sched_state.h"

struct task_struct *ss_schedule_gppcrq(struct task_struct *prev);
void ss_yield_gppcrq(struct task_struct *p, t_t time_passed);
void ss_enqueue_gppcrq(struct task_struct *p);
void ss_dequeue_gppcrq(struct task_struct *p, t_t time_gotten);
