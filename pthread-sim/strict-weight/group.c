#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

#include "vt.h"
#include "driver.h"
#include "lheap.h"
#include "mheap.h"
#include "group.h"
#include "util.h"

extern bool debug;

struct process *grp_new_process(int id, struct group *group) {
    struct process *p = malloc(sizeof(struct process));
    p->process_id = id;
    p->group = group;
	p->group_shard = NULL;
    p->next = NULL;
    return p;
}

struct group *grp_new(struct mheap *mh, int id, int weight) {
    struct group *g = malloc(sizeof(struct group));
    g->group_id = id;
    g->weight = weight;

	int weight_left = weight;
	int shard_id = 0;
	struct group_shard *prev_shard = NULL;
	while (weight_left > 0) {
		struct group_shard *s = malloc(sizeof(struct group_shard));
		s->shard_id = shard_id;
		s->group = g;
		s->weight = min_int(weight_left, SHARD_MAX_WEIGHT);
		s->nthread = 0;
		s->nqueued = 0;
		s->nrunning = 0;
		s->vruntime = 0;
		s->runqueue_head = NULL;
		s->next = NULL;
		pthread_rwlock_init(&s->shard_lock, NULL);
		heap_elem_init(&s->heap_elem, s);
		s->mh = mh;

		if (shard_id == 0) {
			g->shard_head = s;
		} else {
			prev_shard->next = s;
		}
		prev_shard = s;
		weight_left -= s->weight;
		shard_id++;
	}
	printf("init grp %d, weight %d num shards %d \n", g->group_id, g->weight, shard_id);

    g->next = NULL;
    g->nthread = 0;
    g->nqueued = 0;
    g->nrunning = 0;
    g->sleepstart = new_ticks();
	pthread_rwlock_init(&g->group_lock, NULL);
    ticks_gettime(g->sleepstart);
    g->sleeptime = new_ticks();
    g->time = new_ticks();

    return g;
}

// caller must hold group lock
bool grp_shard_is_sleep(struct group_shard *s) {
	return s->nrunning == 0 && s->nqueued == 0;
}

bool grp_shard_dummy(struct group_shard *s) {
	return s->group->group_id == DUMMY;
}

void grp_shard_print(struct group_shard *s) {
	printf(" (gid %d, sid %d, w %d, vrt: %ld, n %d, r %d, q %d)\n", s->group->group_id, s->shard_id, s->weight, s->vruntime, s->nthread, s->nrunning, s->nqueued);
}

void grp_print(struct group *g) {
	printf(" (gid %d, n %d, r %d, q %d, w %d)\n", g->group_id, g->nthread, g->nrunning, g->nqueued, g->weight);
	struct group_shard *s = g->shard_head;
	while (s != NULL) {
		printf("    (sid %d, w %d, vrt: %ld, n %d, r %d, q %d)\n", s->shard_id, s->weight, s->vruntime, s->nthread, s->nrunning, s->nqueued);
		s = s->next;
	}
	printf("\n");
}	

// caller must hold group lock for both groups
int grp_shard_cmp(void *e0, void *e1) {
	struct group_shard *a = (struct group_shard *) e0;
	struct group_shard *b = (struct group_shard *) e1;
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
	if (a->group->group_id < b->group->group_id) return -1;
	if (a->group->group_id > b->group->group_id) return 1;
	return 0;
}

void grp_shard_upd_vruntime(struct group_shard *s, t_t delta) {
    atomic_fetch_add(&s->vruntime, calc_delta(delta, s->weight));
}

// set initial vruntime when group g becomes runnable
// caller must hold group lock
void grp_shard_set_init_vruntime(struct group_shard *s, vt_t min_vt) {
	vt_t nvt = min_vt + s->vruntime;
	if(debug)
		printf("g%d,s%d: grp_set_init_vruntime: mvt %ld new vt %ld\n", s->group->group_id, s->shard_id, min_vt, nvt);
        atomic_store(&s->vruntime, nvt);
}

// remember vruntime for when group becomes runnable again
// caller must hold group lock
void grp_shard_lag_vruntime(struct group_shard *s, vt_t min) {
        atomic_fetch_add(&s->vruntime, -min);
}

// adjust vruntime if group's process didn't run for a complete tick
// caller must hold group lock
bool grp_shard_adjust_vruntime(struct group_shard *s, t_t time_passed, t_t tick_length) {
	if (time_passed < tick_length) {
                int diff = (time_passed - tick_length);
		if (debug) 
			printf("g%d,s%d: adjust vt by %d w %d p %ld t %ld\n", s->group->group_id, s->shard_id, diff, s->weight, time_passed, tick_length);
        grp_shard_upd_vruntime(s, diff);
		return 1;
	}
	return 0;
}

struct group_shard *grp_pick_shard(struct group *g) {
	struct group_shard *min_shard = NULL;
	struct group_shard *curr_shard = g->shard_head;
	while (curr_shard != NULL) {
		if (curr_shard->nthread == 0) {
			min_shard = curr_shard;
			break;
		}
		if (min_shard == NULL ||  (curr_shard->weight / curr_shard->nthread < min_shard->weight / min_shard->nthread)) {
			min_shard = curr_shard;
		}
		curr_shard = curr_shard->next;
	}
	assert(min_shard != NULL);
	return min_shard;
}

// add p to its group shard
// caller must hold shard lock
void grp_add_processL(struct process *p) {
	struct process *curr_head = p->group_shard->runqueue_head;
	if (!curr_head) {
		p->group_shard->runqueue_head = p;
		p->next = NULL;
	} else {
		p->next = curr_head;
		p->group_shard->runqueue_head = p;
	}
	p->group_shard->nqueued += 1;
	atomic_fetch_add(&p->group->nqueued, 1);

}

// remove p from its group.
// caller must hold group lock
struct process *grp_shard_deq_process(struct group_shard *s) {
	struct process *p = s->runqueue_head;
	s->runqueue_head = p->next;
	p->next = NULL;
	s->nqueued -= 1;
	s->group->nqueued -= 1;
	assert(s->nqueued >= 0 && s->group->nqueued >= 0);
	return p;
}

// two threads may try to enqueue grp concurrently, so
// check that it hasn't enqueued yet.
void grp_shard_enqueue(struct group_shard *s) {
	struct lock_heap *lh = mh_choose_heap(s->mh);
	pthread_rwlock_wrlock(&s->group->group_lock);
	if(s->lh == NULL) { // TODO: why would this ever not be the case?
		if(debug) {
			printf("grp_shard_enqueue: g%d,s%d %p\n", s->group->group_id, s->shard_id, lh);
		}
		ticks_gettime(s->group->time);
		ticks_sub(s->group->time, s->group->sleepstart);
		ticks_add(s->group->sleeptime, s->group->time);
		grp_shard_set_init_vruntime(s, mh_min_vrt(lh));
		mh_add_group_shard(s, lh);
	}
	pthread_rwlock_unlock(&s->group->group_lock);
	lh_unlock(lh);
}
