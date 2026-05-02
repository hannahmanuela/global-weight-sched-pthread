#include <stdio.h>

#include "scheduler.h"
#include "gwfs.h"
#include "rr.h"
#include "pcrq.h"
#include "gq.h"

extern int scheduler;
extern int num_groups;
extern int ratio;

void set_scheduler(char *s) {
	if (strcmp(s, "gwfs") == 0) {
		scheduler = GWFS;
	} else if (strcmp(s, "rr") == 0) {
		scheduler = RR;
		if (num_groups == DEF_NUM_GROUPS) num_groups = 1;
	} else if (strcmp(s, "pcrq") == 0) {
		scheduler = PCRQ;
		if (num_groups == DEF_NUM_GROUPS) num_groups = 1;
	} else if (strcmp(s, "gq") == 0) {
		if (num_groups == DEF_NUM_GROUPS) num_groups = 1;
		scheduler = GQ;
	} else {
		fprintf(stderr, "unkown scheduler %s\n", s);
		exit(1);
	}
}


bool is_rr() {
	return scheduler == RR;
}

bool is_pcrq() {
	return scheduler == PCRQ;
}

bool is_gq() {
	return scheduler == GQ;
}

bool ss_schedule(struct sched_state *ss, struct core *c) {
	switch(scheduler) {
	case RR:
		return ss_schedule_rr(ss, c);
	case GWFS:
		return ss_schedule_gwfs(ss, c);
	case PCRQ:
		return ss_schedule_pcrq(ss, c);
	case GQ:
		return ss_schedule_gq(ss, c);
	}
}	

void ss_yield(struct sched_state *ss, struct core *c, struct process *p, t_t t){
	switch(scheduler) {
	case RR:
		ss_yield_rr(ss, c, p, t);
		break;
	case GWFS:
		ss_yield_gwfs(ss, c, p, t);
		break;
	case PCRQ:
		ss_yield_pcrq(ss, c, p, t);
		break;
	case GQ:
		ss_yield_gq(ss, c, p, t);
		break;
	}
}

void ss_enqueue(struct sched_state *ss, struct core *c, struct process *p) {
	switch(scheduler) {
	case RR:
		ss_enqueue_rr(ss, c, p);
		break;
	case GWFS:
		ss_enqueue_gwfs(ss, c, p);
		break;
	case PCRQ:
		ss_enqueue_pcrq(ss, c, p);
		break;
	case GQ:
		ss_enqueue_gq(ss, c, p);
		break;
	}
}

void ss_dequeue(struct sched_state *ss, struct core *c, struct process *p, t_t t) {
	switch(scheduler) {
	case RR:
		ss_dequeue_rr(ss, c, p, t);
		break;
	case GWFS:
		ss_dequeue_gwfs(ss, c, p, t);
		break;
	case PCRQ:
		ss_dequeue_pcrq(ss, c, p, t);
		break;
	case GQ:
		ss_dequeue_gq(ss, c, p, t);
		break;
	}
}
