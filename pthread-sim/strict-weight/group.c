#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

#include "vt.h"
#include "driver.h"
#include "lheap.h"
#include "mheap.h"
#include "group.h"

extern bool debug;

struct process *grp_new_process(struct mheap *mh, int id, struct group *group) {
    struct process *p = malloc(sizeof(struct process));
    p->pid = id;
    p->vruntime = 0;
    p->weight = (group != NULL) ? group->weight : 0;
    pthread_rwlock_init(&p->proc_lock, NULL);
    p->group = group;
    p->next = NULL;
    heap_elem_init(&p->heap_elem, p);
    p->mh = mh;
    p->lh = NULL;
    return p;
}

struct group *grp_new(struct mheap *mh, int id, int weight) {
    struct group *g = malloc(sizeof(struct group));
    g->gid = id;
    g->weight = weight;
    g->nthread = 0;
    g->nqueued = 0;
    g->nrunning = 0;
    g->vruntime = 0;
    g->runqueue_head = NULL;
    g->runtime = 0;
    g->sleepstart = new_ticks();
    ticks_gettime(g->sleepstart);
    g->sleeptime = new_ticks();
    g->time = new_ticks();
    g->mh = mh;
    pthread_rwlock_init(&g->group_lock, NULL);
    return g;
}

// caller must hold group lock
bool grp_is_sleep(struct group *g) {
	return g->nrunning == 0 && g->nqueued == 0;
}

bool proc_dummy(struct process *p) {
	return p->pid == DUMMY;
}

void proc_print(struct process *p) {
	printf("(pid %d(%d) vt %d, w %d)", p->pid, (p->group != NULL) ? p->group->gid : DUMMY, p->vruntime, p->weight);
}	

// caller must hold group lock for both groups
int proc_cmp(void *e0, void *e1) {
	struct process *a = (struct process *) e0;
	struct process *b = (struct process *) e1;
	// Compare by vruntime; lower is higher priority
	if (a->vruntime < b->vruntime) return -1;
	if (a->vruntime > b->vruntime) return 1;
	// Prefer higher weight
	if (a->weight > b->weight) return -1;
	if (a->weight < b->weight) return 1;
	// tie-breaker by gid for determinism
	if (a->pid < b->pid) return -1;
	if (a->pid > b->pid) return 1;
	return 0;
}

void proc_add_vruntime(struct process *p, vt_t vt) {
        atomic_fetch_add(&p->vruntime, vt);
}

// set initial vruntime when group g becomes runnable
// caller must hold group lock
void proc_set_init_vruntime(struct process *p, vt_t min_vt) {
	vt_t nvt = min_vt + p->vruntime;
	if(debug)
		printf("%d(%d): grp_set_init_vruntime: mvt %ld new vt %ld\n", p->pid, p->group->gid, min_vt, nvt);
        atomic_store(&p->vruntime, nvt);
}

// remember vruntime for when group becomes runnable again
// caller must hold group lock
void proc_lag_vruntime(struct process *p, vt_t min) {
        atomic_fetch_add(&p->vruntime, -min);
}

// add p to its group.
// caller must hold group lock
void grp_add_process(struct process *p) {
	struct process *curr_head = p->group->runqueue_head;
	if (!curr_head) {
		p->group->runqueue_head = p;
		p->next = NULL;
	} else {
		p->next = curr_head;
		p->group->runqueue_head = p;
	}
}

// remove p from its group.
// caller must hold group lock
struct process *grp_deq_process(struct group *g) {
	struct process *p = g->runqueue_head;
	g->runqueue_head = p->next;
	p->next = NULL;
	return p;
}

void grp_stats(struct group *g, long sum) {
	if (g->gid == DUMMY)
		return;
	t_t t = ticks_sum(g->sleeptime);
	printf("%d: runtime %d us sleeptime %d us weight %d ticks %0.2f\n", g->gid,
	       g->runtime, t,
	       g->weight, 1.0*g->runtime/(sum-t));
}

