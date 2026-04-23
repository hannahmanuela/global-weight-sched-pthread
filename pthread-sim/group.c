#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

#include "util.h"
#include "vt.h"
#include "driver.h"
#include "heap.h"
#include "mheap.h"
#include "group.h"
#include "mvalue.h"

extern bool debug;
extern bool do_affinity;

static void grp_add_process(struct process *p) {
	struct process *curr_head = p->group->procs;
	if (!curr_head) {
		p->group->procs = p;
		p->next = NULL;
	} else {
		p->next = curr_head;
		p->group->procs = p;
	}
}

struct process *grp_new_process(struct mheap *mh, int id, struct group *group) {
	struct process *p = aligned_alloc(CACHE_LINE_SZ, (sizeof(struct process)));
	p->pid = id;
	p->runtime = 0;
	p->group = group;
	p->next = NULL;
	heap_elem_init(&p->he, 0, group->weight, p);
	// lock_init(&p->lk);
	p->mh = mh;
	p->h = NULL;
	grp_add_process(p);
	return p;
}

struct group *grp_new(struct mheap *mh, int id, int weight, bool using_mv) {
	struct group *g = malloc(sizeof(struct group));
	g->gid = id;
	g->vruntime = 0;
	g->using_mv = using_mv;
	if (using_mv) {
		g->vruntime_mv = mv_new(4);
	} else {
		g->vruntime_mv = NULL;
	}
	g->lag = 0;
	g->weight = weight;
	g->nthread = 0;
	g->procs = NULL;
	g->sleepstart = new_ticks();
	ticks_gettime(g->sleepstart);
	g->sleeptime = new_ticks();
	g->time = new_ticks();
	g->mh = mh;
	return g;
}

void proc_print(struct process *p) {
	printf("[pid %d(%d) vt %lld w %d]", p->pid, p->group->gid,  p->he.vruntime, p->he.weight);
}

vt_t grp_get_vruntime(struct process *p, struct core *c) {
	if (p->group->using_mv) {
		vt_t sum = 0;
		for (int i = 0; i < p->group->vruntime_mv->nvalues; i++) sum += atomic_load(p->group->vruntime_mv->value[i]);
		return sum;
	} else {
		return p->group->vruntime;
	}
}

void grp_set_vruntime(struct process *p, struct core *c, vt_t vt) {
	if(debug)
		printf("%d(%d): grp_set_vruntime: vt %lld\n", p->pid, p->group->gid, vt);
	if (p->group->using_mv) {
		for (int i = 0; i < p->group->vruntime_mv->nvalues; i++) atomic_store(p->group->vruntime_mv->value[i], vt / p->group->vruntime_mv->nvalues);
	} else {
		atomic_store(&p->group->vruntime, vt);
	}
}

vt_t grp_add_vruntime(struct process *p, struct core *c, vt_t vt) {
	if (p->group->using_mv) {
		// not using power of two choices right now, 
		// 		but adding that in w/o locks creates races that we may not like
		int idx_to_use = c_rand(c, p->group->vruntime_mv->nvalues);
		return atomic_fetch_add_explicit(p->group->vruntime_mv->value[idx_to_use], vt, __ATOMIC_RELAXED) * p->group->vruntime_mv->nvalues;
	} else {
		return atomic_fetch_add_explicit(&p->group->vruntime, vt, __ATOMIC_RELAXED);
	}
}

vt_t grp_add_lag(struct process *p, vt_t vt) {
	return atomic_fetch_add_explicit(&p->group->lag, vt, __ATOMIC_RELAXED);
}

vt_t grp_load_lag(struct process *p) {
	return atomic_load(&p->group->lag);
}

static float grp_runtime(struct group *g, long t) {
	float run = 0.0;
	for (struct process *p = g->procs; p != NULL; p = p->next) {
		run += (float)(p->runtime);
	}
	if(debug) {
		printf("  = %d: w %d grp runtime %f fraction %0.2f:\n    ", g->gid, g->weight, run, AVG(run, t));
		for (struct process *p = g->procs; p != NULL; p = p->next) {
			printf("[%d: %ld %0.2f] ", p->pid, p->runtime, AVG(p->runtime, run));
		}
		printf("\n  =");
	} else {
		printf("  = %d: w %d grp runtime %f fraction %0.2f", g->gid, g->weight, run, AVG(run, t));
	}
	return run;
}

void grp_print(struct group *g) {
	printf("[%d: n %d vt %lld lag %lld min_vt_deq %lld]", g->gid, g->nthread, g->vruntime, g->lag, g->min_vt_deq);
}

void grp_stats(struct group *g, long sum) {
	t_t t = ticks_sum(g->sleeptime);
	grp_runtime(g, sum-t);
}

