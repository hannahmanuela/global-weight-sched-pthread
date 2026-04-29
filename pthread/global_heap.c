#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>

#include "vt.h"
#include "util.h"
#include "driver.h"
#include "global_heap.h"
#include "core.h"
#include "group.h"
#include "mheap.h"

//
// for global_heap schedulers (gwfs and rr)
//

bool debug = false;
bool do_affinity = false;
bool do_preempt = false;
bool rr = false;
bool use_localq = false;
bool use_power2_insert = true;
int num_groups = 4;
int scheduler;

struct global_heap *gh_new(int tick_length, int nheap, struct core *cs[], int ncore) {
	struct global_heap *gh = aligned_alloc(CACHE_LINE_SZ, sizeof(struct global_heap));
	gh->tick_length = tick_length;
	gh->mh = mh_new(nheap);
	if(rr) gh->mh1 = mh_new(nheap);
	gh->cs = cs;
	gh->ncore = ncore;
	gh->preempt = PREEMPT(0, MAXWEIGHT, 0);
	queue_init(&gh->q);
	return gh;
}

void gh_print(struct global_heap *gh, struct group *grps[], int n) {
	mh_print(gh->mh);
	printf("= groups %d:\n", n);
	for(int i = 0; i < n; i++) {
		printf("  "); grp_print(grps[i]); printf("\n");
	}
	printf("=\n");
	printf("= cores %d:\n", gh->ncore);
	for(int i = 0; i < gh->ncore; i++) {
		printf("  %d: ", i); core_print(gh->cs[i]); printf("\n");
	}
	printf("=\n");
}

void gh_stats(struct global_heap *gh, struct group *grps[], int n) {
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
