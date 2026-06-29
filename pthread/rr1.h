#include "core.h"

#define RR_HIGH 0
#define RR_LOW 1

struct task_struct *ss_schedule_rr1(struct task_struct *prev);
void ss_yield_rr1(struct task_struct *p, t_t time_passed);
void ss_enqueue_rr1(struct task_struct *p);
void ss_dequeue_rr1(struct task_struct *p, t_t time_gotten);
