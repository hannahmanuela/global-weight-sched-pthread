#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

#include "driver.h"
#include "lheap.h"
#include "mheap.h"
#include "group.h"

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
    g->spec_virt_time = 0;
    g->vt_inc = 0;
    g->last_virt_time = 0;
    g->runqueue_head = NULL;
    g->next = NULL;
    g->sleeptime = 0;
    g->runtime = 0;
    g->sleepstart = new_ticks();
    ticks_gettime(g->sleepstart);
    g->time = new_ticks();
    heap_elem_init(&g->heap_elem, g);
    pthread_rwlock_init(&g->group_lock, NULL);
    return g;
}

int grp_cmp(void *e0, void *e1) {
	struct group *a = (struct group *) e0;
	struct group *b = (struct group *) e1;
	if (a->threads_queued == 0) return 1;
	if (b->threads_queued == 0) return -1;
	// Compare by spec_virt_time; lower is higher priority
	if (a->spec_virt_time < b->spec_virt_time) return -1;
	if (a->spec_virt_time > b->spec_virt_time) return 1;
	// Prefer higher weight
	if (a->weight > b->weight) return -1;
	if (a->weight < b->weight) return 1;
	// tie-breaker by group_id for determinism
	if (a->group_id < b->group_id) return -1;
	if (a->group_id > b->group_id) return 1;
	return 0;
}

void grp_set_spec_virt_time_avg(struct group *g, int val) {
	g->spec_virt_time = val;
	g->lh->heap->sum += val;
	g->lh->heap->n += 1;
	g->mh->tot_weight += g->weight;
}

void grp_clear_spec_virt_time_avg(struct group *g) {
	g->lh->heap->n -= 1;
	g->lh->heap->sum -= g->spec_virt_time;
	g->mh->tot_weight -= g->weight;
}

void grp_upd_spec_virt_time_avg(struct group *g, int delta) {
	g->spec_virt_time += delta;
	g->lh->heap->sum += delta;
}

// XXX obsolete when gl_avg_spec_virt_time is obsolete
int grp_get_spec_virt_time(struct group *g) {
	pthread_rwlock_rdlock(&g->group_lock);
	int curr_spec_virt_time = g->spec_virt_time;
	if (g->threads_queued == 0) {
		curr_spec_virt_time = INT_MAX;
	}
	pthread_rwlock_unlock(&g->group_lock);
	return curr_spec_virt_time;
}

// set initial spec_virt_time when group g becomes runnable
// caller must hold group lock
void grp_set_init_spec_virt_time(struct group *g, int avg) {
	int initial_virt_time = avg;
	if (g->vt_inc > 0) {
		if (g->last_virt_time > initial_virt_time + g->vt_inc) {
			initial_virt_time = g->last_virt_time; // adding back left over lag only if its still ahead
		} else {
			initial_virt_time += g->vt_inc;
		}
	} else if (g->vt_inc < 0) {
		initial_virt_time += g->vt_inc; // negative lag always carries over? maybe limit it?
	}
	printf("%d: grp_set_init_spec_virt_time: lvt %d inc %d avg %d vt %d\n", g->group_id, g->last_virt_time, g->vt_inc, avg, initial_virt_time);
	grp_set_spec_virt_time_avg(g, initial_virt_time);
}

// remember spec_virt_time for when group becomes runnable again
// caller must group lock
void grp_store_spec_virt_time_inc(struct group *g, int vt_inc) {
	g->vt_inc = vt_inc;
	g->last_virt_time = g->spec_virt_time;
	printf("%d: grp_store_spec_virt_time_inc: svt %d vt_inc %d\n", g->group_id, g->last_virt_time, g->vt_inc);
	grp_clear_spec_virt_time_avg(g);
}


// adjust spec_virt_time if group's process didn't run for a complete tick
int grp_adjust_spec_virt_time(struct process *p, int time_passed, int tick_length) {
	if (time_passed < tick_length) {
		int old = vt_inc(tick_length, p->tot_weight, p->group->weight);
		int new = vt_inc(time_passed, p->tot_weight, p->group->weight);
		int diff = -old + new;
		printf("%d: adjust svt by %d old %d new %d\n", p->group->group_id, diff, old, new);
		grp_upd_spec_virt_time_avg(p->group, diff);
		return diff;
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

