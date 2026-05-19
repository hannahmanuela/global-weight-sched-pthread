#include "core.h"
#include "group.h"

bool ss_account_schedule_gwfs();
void ss_yield_gwfs(struct core *c, struct task_struct *p, t_t time_passed);
void ss_enqueue_gwfs(struct task_struct *p);
void ss_dequeue_gwfs(struct core *c, struct task_struct *p, t_t time_gotten);
