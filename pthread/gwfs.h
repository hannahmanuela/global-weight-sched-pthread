#include "core.h"
#include "group.h"

bool ss_schedule_gwfs(struct core *c);
void ss_yield_gwfs(struct core *c, struct task_struct *p, t_t time_passed);
void ss_enqueue_gwfs(struct core *c, struct task_struct *p);
void ss_dequeue_gwfs(struct core *c, struct task_struct *p, t_t time_gotten);
