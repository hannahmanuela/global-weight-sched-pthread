#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>

#include "scheduler.h"
#include "vt.h"
#include "util.h"
#include "driver.h"
#include "sched_state.h"
#include "core.h"
#include "group.h"
#include "mheap.h"

//
// for sched_state schedulers (gwfs and rr)
//

bool debug = false;
bool do_affinity = false;
bool do_preempt = false;
bool use_power2_insert = true;
int num_groups = DEF_NUM_GROUPS;
int scheduler;
int ratio = 1;
bool do_latency = false;

struct sched_state *ss_new(int tick_length, int nheap, struct core *cs[], int ncore) {
	struct sched_state *ss = aligned_alloc(CACHE_LINE_SZ, sizeof(struct sched_state));
	ss->tick_length = tick_length;
	ss->mh = mh_new(nheap);
	if(is_rr()) ss->mh1 = mh_new(nheap);
	ss->cs = cs;
	ss->ncore = ncore;
	ss->preempt = PREEMPT(0, MAXWEIGHT, 0);
	if(is_gq()) 
		queue_init(&ss->q);
	return ss;
}

struct core *ss_choose_core(struct sched_state *ss, struct core *c) {
	int i = c_rand(c, ss->ncore);
	return ss->cs[i];
}

void ss_print(struct sched_state *ss, struct group *grps[], int n) {
	mh_print(ss->mh);
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
