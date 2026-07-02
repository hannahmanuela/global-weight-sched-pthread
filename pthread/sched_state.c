#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "vt.h"
#include "util.h"
#include "driver.h"
#include "sched_state.h"
#include "core.h"
#include "group.h"
#include "mheap.h"
#include "gwfs.h"
#include "rr.h"
#include "rr1.h"
#include "pcrq.h"
#include "gppcrq.h"
#include "gq.h"

//
// sched_state for schedulers such as gwfs and rr
//

bool debug = false;
bool do_affinity = false;
bool do_preempt = false;
bool delay_yield = false;
bool use_power2_insert = true;
bool use_runningq = false;
int num_groups = DEF_NUM_GROUPS;
int ratio = 1;
bool do_latency = false;
bool use_rao_int = false;
int scheduler;
struct sched_state *ss_global;

struct sched_state *ss_new(int tick_length, int nheap, struct core *cs[], int ncore, is_lt_elem_t lt, is_min_elem_t min) {
	struct sched_state *ss = aligned_alloc(CACHE_LINE_SZ, ALIGN_UP(sizeof(struct sched_state), CACHE_LINE_SZ));
	ss_global = ss;
	ss->tick_length = tick_length;
	if (is_pcrq() || is_gppcrq()) {
		ss->mh = mh_new(ncore);
	} else {
		ss->mh = mh_new(nheap);
	}
	if(is_rr()) {
		ss->mh_l = mh_new(nheap);
	}
	ss->cs = cs;
	ss->ncore = ncore;
	ss->is_lt_elem = lt;
	ss->is_min_elem = min;
	ss->preempt = PREEMPT(0, MAXWEIGHT, 0);
	dl_init(&ss->preemptq);
	if(is_gq()) {
		queue_init(&ss->q_h);
		queue_init(&ss->q_l);
	}
	if(use_runningq) {
		ss->mh_r = mh_new(nheap);
	}

	switch (scheduler) {
	case GWFS:
		ss->sched = (struct scheduler) {
			ss_account_schedule_gwfs, ss_yield_gwfs, ss_enqueue_gwfs, ss_dequeue_gwfs
		};
		break;
	case RR:
		ss->sched = (struct scheduler) {
			ss_schedule_rr, ss_yield_rr, ss_enqueue_rr, ss_dequeue_rr
		};
		break;
	case PCRQ:
		ss->sched = (struct scheduler) {
			ss_schedule_pcrq, ss_yield_pcrq, ss_enqueue_pcrq, ss_dequeue_pcrq
		};
		break;
	case GPPCRQ:
		ss->sched = (struct scheduler) {
			ss_schedule_gppcrq, ss_yield_gppcrq, ss_enqueue_gppcrq, ss_dequeue_gppcrq
		};
		break;
	case GQ:
		ss->sched = (struct scheduler) {
			ss_schedule_gq, ss_yield_gq, ss_enqueue_gq, ss_dequeue_gq
		};
		break;
	case RR1:
		ss->sched = (struct scheduler) {
			ss_schedule_rr1, ss_yield_rr1, ss_enqueue_rr1, ss_dequeue_rr1
		};
		break;
	}

	return ss;
}

void ss_print(struct sched_state *ss, struct group *grps[], int n) {
	proc_mh_print(ss->mh);
	printf("= groups %d:\n", n);
	for(int i = 0; i < n; i++) {
		printf("  "); grp_print(grps[i]); printf("\n");
	}
	printf("=\n");
	printf("= cores %d:\n", ss->ncore);
	for(int i = 0; i < ss->ncore; i++) {
		printf("  %d: ", i); core_print(ss->cs[i]); printf("\n");
	}
	printf("=\n");
}

void ss_stats(struct sched_state *ss, struct group *grps[], int n) {
	t_t *ticks = new_ticks();
	ticks_gettime(ticks);
	t_t tot = ticks_sum(ticks);
	ticks_getwork(ticks);
	t_t work = ticks_sum(ticks);
	ticks_getidle(ticks);
	t_t idle = ticks_sum(ticks);
	printf("= stats total ticks %ld us work %ld us idle %ld us\n", tot, work, idle);
	for(int i = 0; i < n; i++) {
		grp_stats(grps[i], tot); printf("\n");
	}
}

void set_scheduler(char *s) {
	if (strcmp(s, "gwfs") == 0) {
		scheduler = GWFS;
	} else if (strcmp(s, "rr") == 0) {
		scheduler = RR;
		if (num_groups == DEF_NUM_GROUPS) num_groups = 1;
	} else if (strcmp(s, "rr1") == 0) {
		scheduler = RR1;
		if (num_groups == DEF_NUM_GROUPS) num_groups = 1;
	} else if (strcmp(s, "pcrq") == 0) {
		scheduler = PCRQ;
		if (num_groups == DEF_NUM_GROUPS) num_groups = 1;
	} else if (strcmp(s, "gppcrq") == 0) {
		scheduler = GPPCRQ;
		if (num_groups == DEF_NUM_GROUPS) num_groups = 1;
	} else if (strcmp(s, "gq") == 0) {
		if (num_groups == DEF_NUM_GROUPS) num_groups = 1;
		scheduler = GQ;
	} else {
		fprintf(stderr, "unkown scheduler %s\n", s);
		exit(1);
	}
}

bool is_gwfs() {
	return scheduler == GWFS;
}

bool is_rr() {
	return scheduler == RR;
}

bool is_rr1() {
	return scheduler == RR1;
}

bool is_pcrq() {
	return scheduler == PCRQ;
}

bool is_gppcrq() {
	return scheduler == GPPCRQ;
}

bool is_gq() {
	return scheduler == GQ;
}

bool ss_schedule(struct sched_state *ss, struct core *c) {
	c->process = ss->sched.schedule(c->process);
	return c->process == NULL;
}	

void ss_yield(struct sched_state *ss, struct core *c, struct task_struct *p, t_t t) {
	ss->sched.yield(p, t);
	if (!delay_yield)
		c->process = NULL;
}

void ss_enqueue(struct sched_state *ss, struct core *c, struct task_struct *p) {
	ss->sched.enqueue(p);
	if(c->process == p) {
		c->process = NULL;
	}
}

void ss_dequeue(struct sched_state *ss, struct core *c, struct task_struct *p, t_t t) {
	ss->sched.dequeue(p, t);
	c->process = NULL;
}
