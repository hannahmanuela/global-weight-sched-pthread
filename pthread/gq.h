#include "core.h"

bool gh_schedule_gq(struct global_heap *gh, struct core *c);
void gh_yield_gq(struct global_heap *gh, struct core *c, struct process *p, t_t time_passed);
void gh_enqueue_gq(struct global_heap *gh, struct core *c, struct process *p);
void gh_dequeue_gq(struct global_heap *gh, struct core *c, struct process *p, t_t time_gotten);
