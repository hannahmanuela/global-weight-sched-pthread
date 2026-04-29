#include <stdio.h>

#include "scheduler.h"
#include "gwfs.h"
#include "rr.h"
#include "pcrq.h"

extern int scheduler;
extern int num_groups;
extern int ratio;

void set_scheduler(char *s) {
	if (strcmp(s, "gwfs") == 0) {
		scheduler = GWFS;
	} else if (strcmp(s, "rr") == 0) {
		scheduler = RR;
		if (num_groups == 4) num_groups = 1;
	} else if (strcmp(s, "pcrq") == 0) {
		scheduler = PCRQ;
	} else {
		fprintf(stderr, "unkown scheduler %s\n", s);
		exit(1);
	}
}


bool is_rr() {
	return scheduler == RR;
}

bool gh_schedule(struct global_heap *gh, struct core *c) {
	switch(scheduler) {
	case RR:
		return gh_schedule_rr(gh, c);
	case GWFS:
		return gh_schedule_gwfs(gh, c);
	case PCRQ:
		return gh_schedule_pcrq(gh, c);
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
	case PCRQ:
		gh_yield_pcrq(gh, c, p, t);
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
	case PCRQ:
		gh_enqueue_pcrq(gh, c, p);
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
	case PCRQ:
		gh_dequeue_pcrq(gh, c, p, t);
		break;
	}
}
