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
#include "gwfs.h"
#include "rr.h"
#include "pcrq.h"
#include "gq.h"

//
// for sched_state schedulers (gwfs and rr)
//

bool debug = false;
bool do_affinity = false;
bool do_preempt = false;
bool delay_yield = false;
bool use_power2_insert = true;
int num_groups = DEF_NUM_GROUPS;
int ratio = 1;
bool do_latency = false;
int scheduler;
struct sched_state *ss_global;

struct sched_state *ss_new(int tick_length, int nheap, struct core *cs[], int ncore) {
	struct sched_state *ss = aligned_alloc(CACHE_LINE_SZ, ALIGN_UP(sizeof(struct sched_state), CACHE_LINE_SZ));
	ss_global = ss;
	ss->tick_length = tick_length;
	ss->mh = mh_new(nheap);
	if(is_rr()) ss->mh1 = mh_new(nheap);
	ss->cs = cs;
	ss->ncore = ncore;
	ss->preempt = PREEMPT(0, MAXWEIGHT, 0);
	dl_init(&ss->preemptq);
	if(is_gq()) queue_init(&ss->q);

	switch (scheduler) {
	case GWFS:
		ss->schedv1 = (struct schedulerv1) {
			ss_account_schedule_gwfs, ss_yield_gwfs, ss_enqueue_gwfs, ss_dequeue_gwfs
		};
		break;
	case RR:
		ss->schedv1 = (struct schedulerv1) {
			ss_schedule_rr, ss_yield_rr, ss_enqueue_rr, ss_dequeue_rr
		};
		break;
	case PCRQ:
		ss->sched = (struct scheduler) {
			ss_schedule_pcrq, ss_yield_pcrq, ss_enqueue_pcrq, ss_dequeue_pcrq
		};
		break;
	case GQ:
		ss->sched = (struct scheduler) {
			ss_schedule_gq, ss_yield_gq, ss_enqueue_gq, ss_dequeue_gq
		};
		break;
	}

	return ss;
}

struct core *ss_choose_core(struct sched_state *ss, struct core *c) {
	int i = c_rand(ss->ncore);
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
