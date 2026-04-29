#include <stdio.h>

#include "scheduler.h"
#include "gwfs.h"
#include "rr.h"

extern int scheduler;

bool is_rr() {
	return scheduler == RR;
}

bool gh_schedule(struct global_heap *gh, struct core *c) {
	switch(scheduler) {
	case RR:
		return gh_schedule_rr(gh, c);
	case GWFS:
		return gh_schedule_gwfs(gh, c);
	}
}	

void gh_yield(struct global_heap *gh, struct core *c, struct process *p, t_t t){
	switch(scheduler) {
	case RR:
		gh_yield_rr(gh, c, p, t);
		break;
	case GWFS:
		gh_yield_gwfs(gh, c, p, t);
		break;
	}
}

void gh_enqueue(struct global_heap *gh, struct core *c, struct process *p) {
	switch(scheduler) {
	case RR:
		gh_enqueue_rr(gh, c, p);
		break;
	case GWFS:
		gh_enqueue_gwfs(gh, c, p);
		break;
	}
}

void gh_dequeue(struct global_heap *gh, struct core *c, struct process *p, t_t t) {
	switch(scheduler) {
	case RR:
		gh_dequeue_rr(gh, c, p, t);
		break;
	case GWFS:
		gh_dequeue_gwfs(gh, c, p, t);
		break;
	}
}
