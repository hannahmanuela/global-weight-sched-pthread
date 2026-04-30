#include "core.h"
#include "group.h"
#include "global_heap.h"

bool gh_schedule_gwfs(struct global_heap *gh, struct core *c);
void gh_yield_gwfs(struct global_heap *gh, struct core *c, struct process *p, t_t time_passed);
void gh_enqueue_gwfs(struct global_heap *gh, struct core *c, struct process *p);
void gh_dequeue_gwfs(struct global_heap *gh, struct core *c, struct process *p, t_t time_gotten);
