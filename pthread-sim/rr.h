#include "core.h"

struct process *gh_schedule_rr(struct global_heap *gh, struct core *c);
void gh_yield_rr(struct global_heap *gh, struct core *c, struct process *p, t_t time_passed);
void gh_enqueue_rr(struct global_heap *gh, struct core *c, struct process *p);
void gh_dequeue_rr(struct global_heap *gh, struct core *c, struct process *p, t_t time_gotten);
