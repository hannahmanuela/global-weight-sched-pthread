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

struct process *grp_new_process(int id, struct group *group) {
    struct process *p = malloc(sizeof(struct process));
    p->process_id = id;
    p->group = group;
    p->next = NULL;
    return p;
}

struct group *grp_new(int id, int weight) {
    struct group *g = malloc(sizeof(struct group));
    g->group_id = id;
    g->weight = weight;
    g->num_threads = 0;
    g->threads_queued = 0;
    g->vruntime = 0;
    g->runqueue_head = NULL;
    g->next = NULL;
    g->runtime = 0;
    g->sleepstart = new_ticks();
    ticks_gettime(g->sleepstart);
    g->sleeptime = new_ticks();
    g->time = new_ticks();
    heap_elem_init(&g->heap_elem, g);
    pthread_rwlock_init(&g->group_lock, NULL);
    return g;
}


bool grp_dummy(struct group *g) {
	return g->group_id == DUMMY;
}

void grp_print(struct group *g) {
	printf("(gid %d vt %d, n %d, q %d, w %d)", g->group_id, g->vruntime, g->num_threads, g->threads_queued, g->weight);
}	

int grp_cmp(void *e0, void *e1) {
	struct group *a = (struct group *) e0;
	struct group *b = (struct group *) e1;
	if (a->threads_queued == 0) return 1;
	if (b->threads_queued == 0) return -1;
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

void grp_upd_vruntime(struct group *g, t_t delta) {
        atomic_fetch_add(&g->vruntime, calc_delta(delta, g->weight));
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
// caller must group lock
void grp_lag_vruntime(struct group *g, vt_t min) {
        atomic_fetch_add(&g->vruntime, -min);
}

// adjust vruntime if group's process didn't run for a complete tick
bool grp_adjust_vruntime(struct group *g, t_t time_passed, t_t tick_length) {
	if (time_passed < tick_length) {
                int diff = (time_passed - tick_length);
		if(debug) 
			printf("%d: adjust vt by %ld w %d p %ld t %ld\n", g->group_id, diff, g->weight, time_passed, tick_length);
                grp_upd_vruntime(g, diff);
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
	p->group->threads_queued += 1;
}

// remove p from its group.
// caller must hold group lock
struct process *grp_deq_process(struct group *g) {
	struct process *p = g->runqueue_head;
	g->runqueue_head = p->next;
	p->next = NULL;
	g->threads_queued -= 1;
	assert(g->threads_queued >= 0);
	return p;
}
