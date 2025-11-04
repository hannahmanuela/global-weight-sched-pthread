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
    p->process_id = id;
    p->group = group;
    p->next = NULL;
    p->mh = mh;
    p->vruntime = 0;
    p->weight = 0;
    heap_elem_init(&p->heap_elem, p);
    pthread_rwlock_init(&p->proc_lock, NULL);
    return p;
}

struct group *grp_new(struct mheap *mh, int id, int weight) {
    struct group *g = malloc(sizeof(struct group));
    g->group_id = id;
    g->weight = weight;
    g->nthread = 0;
    g->nqueued = 0;
    g->nrunning = 0;
    g->vruntime = 0;
    g->runqueue_head = NULL;
    g->next = NULL;
    g->runtime = 0;
    g->sleepstart = new_ticks();
    ticks_gettime(g->sleepstart);
    g->sleeptime = new_ticks();
    g->time = new_ticks();
    heap_elem_init(&g->heap_elem, g);
    g->mh = mh;
    pthread_rwlock_init(&g->group_lock, NULL);
    return g;
}

// caller must hold group lock
bool grp_is_sleep(struct group *g) {
	return g->nrunning == 0 && g->nqueued == 0;
}

bool proc_dummy(struct process *p) {
	return p->process_id == DUMMY;
}

void proc_print(struct process *p) {
	printf("(proc %d(%d) vt %d, w %d)", p->process_id, (p->group != NULL) ? p->group->group_id : DUMMY, p->vruntime, p->weight);
}	

// caller must hold group lock for both groups
int grp_cmp(void *e0, void *e1) {
	struct group *a = (struct group *) e0;
	struct group *b = (struct group *) e1;
	// ignore group with no runnable threads queued
	// (it may be still in the heap if it has running threads.)
	if (a->nqueued == 0) return 1;
	if (b->nqueued == 0) return -1;
	// Compare by vruntime; lower is higher priority
	if (a->vruntime < b->vruntime) return -1;
	if (a->vruntime > b->vruntime) return 1;
	// Prefer higher weight
	if (a->weight > b->weight) return -1;
	if (a->weight < b->weight) return 1;
	// tie-breaker by group_id for determinism
	if (a->group_id < b->group_id) return -1;
	if (a->group_id > b->group_id) return 1;
	return 0;
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
	// tie-breaker by group_id for determinism
	if (a->process_id < b->process_id) return -1;
	if (a->process_id > b->process_id) return 1;
	return 0;
}

void proc_upd_vruntime(struct process *p, t_t delta) {
        atomic_fetch_add(&p->vruntime, calc_delta(delta, p->weight));
}

// set initial vruntime when group g becomes runnable
// caller must hold group lock
void grp_set_init_vruntime(struct group *g, vt_t min_vt) {
	vt_t nvt = min_vt + g->vruntime;
	if(debug)
		printf("%d: grp_set_init_vruntime: mvt %ld new vt %ld\n", g->group_id, min_vt, nvt);
        atomic_store(&g->vruntime, nvt);
}

// remember vruntime for when group becomes runnable again
// caller must hold group lock
void grp_lag_vruntime(struct group *g, vt_t min) {
        atomic_fetch_add(&g->vruntime, -min);
}

// adjust vruntime if group's process didn't run for a complete tick
// caller must hold group lock
bool proc_adjust_vruntime(struct process *p, t_t time_passed, t_t tick_length) {
	if (time_passed < tick_length) {
                int diff = (time_passed - tick_length);
		if(debug) 
			printf("%d(%d): adjust vt by %ld w %d p %ld t %ld\n", p->process_id, p->group->group_id, diff, p->weight, time_passed, tick_length);
                proc_upd_vruntime(p, diff);
		return 1;
	}
	return 0;
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
	p->group->nqueued += 1;
	p->weight = p->group->weight/p->group->nqueued;
}

// remove p from its group.
// caller must hold group lock
struct process *grp_deq_process(struct group *g) {
	struct process *p = g->runqueue_head;
	g->runqueue_head = p->next;
	p->next = NULL;
	g->nqueued -= 1;
	assert(g->nqueued >= 0);
	return p;
}

// two threads may try to enqueue grp concurrently, so
// check that it hasn't enqueued yet.
void grp_enqueue(struct group *g) {
	struct lock_heap *lh = mh_choose_heap(g->mh);
	pthread_rwlock_wrlock(&g->group_lock);
	if(g->lh == NULL) {
		if(debug) {
			printf("grp_enqueue: %d %p\n", g->group_id, lh);
		}
		ticks_gettime(g->time);
		ticks_sub(g->time, g->sleepstart);
		ticks_add(g->sleeptime, g->time);
		grp_set_init_vruntime(g, mh_min(lh));
		mh_add_group(g, lh);
	}
	pthread_rwlock_unlock(&g->group_lock);
	lh_unlock(lh);
}
