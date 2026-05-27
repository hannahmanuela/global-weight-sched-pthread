#include "core.h"

struct task_struct *ss_schedule_gq(struct task_struct *prev);
void ss_yield_gq(struct task_struct *p, t_t time_passed);
void ss_enqueue_gq(struct task_struct *p);
void ss_dequeue_gq(struct task_struct *p, t_t time_gotten);
