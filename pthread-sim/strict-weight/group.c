#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

#include "util.h"
#include "vt.h"
#include "driver.h"
#include "lheap.h"
#include "mheap.h"
#include "group.h"

extern bool debug;

struct process *grp_new_process(struct mheap *mh, int id, struct group *group) {
    struct process *p = malloc(sizeof(struct process));
    p->pid = id;
    p->runtime = 0;
    pthread_rwlock_init(&p->proc_lock, NULL);
    p->group = group;
    p->next = NULL;
    heap_elem_init(&p->he, 0, group->weight, p);
    p->mh = mh;
    p->lh = NULL;
    if(p->group)
	    grp_add_process(p);
    return p;
}

struct group *grp_new(struct mheap *mh, int id, int weight) {
    struct group *g = malloc(sizeof(struct group));
    g->gid = id;
    g->weight = weight;
    g->nthread = 0;
    g->nqueued = 0;
    g->procs = NULL;
    g->sleepstart = new_ticks();
    ticks_gettime(g->sleepstart);
    g->sleeptime = new_ticks();
    g->time = new_ticks();
    g->mh = mh;
    pthread_rwlock_init(&g->group_lock, NULL);
    return g;
}

vt_t grp_slot(struct process *p, int nthread) {
	return calc_delta(p->mh->tick_length, p->he.weight) * (nthread-1);
}

void proc_print(struct process *p) {
	printf("(pid %d(%d) vt %d, w %d)", p->pid, p->group->gid,  p->he.vruntime, p->he.weight);
}	

// caller must hold group lock for both groups
int proc_cmp(struct heap_elem *a, struct heap_elem *b) {
	// Compare by vruntime; lower is higher priority
	if (a->vruntime < b->vruntime) return -1;
	if (a->vruntime > b->vruntime) return 1;
	// Prefer higher weight
	if (a->weight > b->weight) return -1;
	if (a->weight < b->weight) return 1;
	// tie-breaker by gid for determinism
	//if (a->pid < b->pid) return -1;
	//if (a->pid > b->pid) return 1;
	return 0;
}

void proc_add_vruntime(struct process *p, vt_t vt) {
        atomic_fetch_add(&p->he.vruntime, vt);
}

void proc_insert_mh(struct process *p, struct lheap *lh) {
	mh_add_process(p, lh);
        // atomic_fetch_add(&p->group->nqueued, 1);    // for debugging
}

// set initial vruntime when group g becomes runnable
// caller must hold group lock
void proc_set_init_vruntime(struct process *p, vt_t min_vt) {
	vt_t nvt = min_vt + p->he.vruntime;
	if(debug)
		printf("%d(%d): grp_set_init_vruntime: mvt %ld new vt %ld\n", p->pid, p->group->gid, min_vt, nvt);
        atomic_store(&p->he.vruntime, nvt);
}

// remember vruntime for when group becomes runnable again
// caller must hold group lock
void proc_lag_vruntime(struct process *p, vt_t min) {
        atomic_fetch_add(&p->he.vruntime, -min);
}


// add p to its groups for stats
void grp_add_process(struct process *p) {
	struct process *curr_head = p->group->procs;
	if (!curr_head) {
		p->group->procs = p;
		p->next = NULL;
	} else {
		p->next = curr_head;
		p->group->procs = p;
	}
}

float grp_runtime(struct group *g) {
	float run = 0.0;
	for (struct process *p = g->procs; p != NULL; p = p->next) {
		run += (float)(p->runtime);
	}
	return run;
}

void grp_stats(struct group *g, long sum) {
	t_t t = ticks_sum(g->sleeptime);
	float run = grp_runtime(g);
	// printf("%d: runtime %0.2f us sleeptime %d us weight %d ticks %0.2f\n", g->gid,
	// run, t, g->weight, AVG(run, (sum-t)));
	printf("%d: ticks %0.2f, ", g->gid, AVG(run, (sum-t)));
}

