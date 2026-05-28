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

extern bool debug;
extern bool do_affinity;

static void grp_add_process(struct task_struct *p) {
	struct task_struct *curr_head = p->group->procs;
	if (!curr_head) {
		p->group->procs = p;
		p->next = NULL;
	} else {
		p->next = curr_head;
		p->group->procs = p;
	}
}

struct task_struct *grp_new_process(struct mheap *mh, int id, struct group *group) {
	struct task_struct *p = aligned_alloc(CACHE_LINE_SZ, ALIGN_UP(sizeof(struct task_struct), CACHE_LINE_SZ));
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

struct group *grp_new(struct mheap *mh, int id, int weight) {
	struct group *g = malloc(sizeof(struct group));
	g->gid = id;
	g->vruntime = 0;
	g->offset = 0;
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

void proc_print(struct task_struct *p) {
	printf("[pid %d(%d) vt %lld w %d]", p->pid, p->group->gid,  p->he.vruntime, p->he.weight);
}	

void grp_set_vruntime(struct task_struct *p, vt_t vt) {
	if(debug)
		printf("%d(%d): grp_set_vruntime: vt %lld\n", p->pid, p->group->gid, vt);
	atomic_store(&p->group->vruntime, vt);
}

vt_t grp_add_vruntime(struct task_struct *p, vt_t vt) {
	return atomic_fetch_add_explicit(&p->group->vruntime, vt, __ATOMIC_RELAXED);
}

vt_t grp_add_offset(struct task_struct *p, vt_t vt) {
	return atomic_fetch_add_explicit(&p->group->offset, vt, __ATOMIC_RELAXED);
}

vt_t grp_load_offset(struct task_struct *p) {
	return atomic_load(&p->group->offset);
}

static float grp_runtime(struct group *g, long t) {
	float run = 0.0;
	for (struct task_struct *p = g->procs; p != NULL; p = p->next) {
		run += (float)(p->runtime);
	}
	if(debug) {
		printf("  = %d: w %d grp runtime %f fraction %0.2f:\n    ", g->gid, g->weight, run, AVG(run, t));
		for (struct task_struct *p = g->procs; p != NULL; p = p->next) {
			printf("[%d: %ld %0.2f] ", p->pid, p->runtime, AVG(p->runtime, run));
		}
		printf("\n  =");
	} else {
		printf("  = %d: w %d grp runtime %f fraction %0.2f", g->gid, g->weight, run, AVG(run, t));
	}
	return run;
}

void grp_print(struct group *g) {
	printf("[%d: n %d vt %lld offset %lld min_vt_deq %lld]", g->gid, g->nthread, g->vruntime, g->offset, g->min_vt_deq);
}

void grp_stats(struct group *g, long sum) {
	t_t t = ticks_sum(g->sleeptime);
	grp_runtime(g, sum-t);
}

