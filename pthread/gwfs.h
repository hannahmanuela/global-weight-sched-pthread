#include "core.h"
#include "group.h"

struct task_struct *ss_account_schedule_gwfs(struct task_struct *prev);
void ss_yield_gwfs(struct task_struct *p, t_t time_passed);
void ss_enqueue_gwfs(struct task_struct *p);
void ss_dequeue_gwfs(struct task_struct *p, t_t time_gotten);
