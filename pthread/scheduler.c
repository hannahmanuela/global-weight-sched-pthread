#include <stdio.h>
#include <assert.h>

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
	return ss->sched.schedule(ss, c);
}	

void ss_yield(struct sched_state *ss, struct core *c, struct task_struct *p, t_t t){
	assert(c->process == p);
	return ss->sched.yield(ss, c, p, t);
}

void ss_enqueue(struct sched_state *ss, struct core *c, struct task_struct *p) {
	return ss->sched.enqueue(ss, c, p);
}

void ss_dequeue(struct sched_state *ss, struct core *c, struct task_struct *p, t_t t) {
	assert(c->process == p);
	return ss->sched.dequeue(ss, c, p, t);
}
