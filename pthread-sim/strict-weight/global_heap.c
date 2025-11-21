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

bool debug;
bool do_affinity;

struct global_heap *gh_new(int tick_length, int cmp(struct heap_elem *, struct heap_elem *), int n) {
	struct global_heap *gh = aligned_alloc(CACHE_LINE_SZ, sizeof(struct global_heap));
	gh->tick_length = tick_length;
	gh->mh = mh_new(cmp, n);
	return gh;
}

// Select next process to run
struct process *gh_schedule(struct global_heap *gh, struct core *c) {
	struct process *min_proc;
	if(do_affinity && c->process && ((min_proc = mh_is_min(c)) != NULL)) {
		c->hit++;
	} else {
		min_proc = mh_min_proc(gh->mh, c);
	}
	if (min_proc == NULL) {
		return NULL;
	}

	if(debug) {
		printf("%d: schedule %d(%d) vt %u\n", c->cid, min_proc->pid, min_proc->group->gid, min_proc->he.vruntime);
		mh_print(min_proc->mh);
	}
	if(c->fd > 0) {
		c_log_append(c, min_proc);
	}

	return min_proc;
}

static void enq_proc_vt(struct global_heap *gh, struct core *c, struct process *p, struct heap *h) {
	vt_t wvt = calc_delta(gh->tick_length, p->he.weight);
	vt_t my_vt = grp_add_vruntime(p, wvt);
	assert(my_vt >= p->he.vruntime);  // overflow?
	p->he.vruntime = my_vt;
	mh_add_process(c, p, h);
}

// Add p to group and make p runnable
void gh_enqueue(struct global_heap *gh, struct core *c, struct process *p) {
	struct heap *h = mh_choose_heap(p->mh, c);
	assert(p->h == NULL);

	int old_nthread = atomic_fetch_add(&p->group->nthread, 1);
	if(old_nthread == 0) {  // group has become runnable
		ticks_gettime(p->group->time);
		ticks_sub(p->group->time, p->group->sleepstart);
		ticks_add(p->group->sleeptime, p->group->time);
		vt_t lag = p->group->vruntime - p->group->min_vt_deq;
		vt_t h_min = mh_min_vt(h);
		if(p->group->min_vt_deq > h_min) {
			lag += (p->group->min_vt_deq-h_min);
		}
		vt_t vt = mh_min_vt(h) + lag;
		grp_set_vruntime(p, vt);
	}

	enq_proc_vt(gh, c, p, h);

	if(debug) {
		printf("%d(%d): enqueue nthread %d lh %p vt %u gvt %d\n", p->pid, p->group->gid, p->group->nthread, p->h, p->he.vruntime, p->group->vruntime);
		mh_print(p->group->mh);
	}
}

// proc may have run for less than its allocated time; in that
// case adjust the proc's group vruntime.
static void grp_adjust_vruntime(struct global_heap *gh, struct process *p, t_t time_passed) {
	p->runtime += time_passed;
	vt_t vt = calc_delta(time_passed, p->he.weight);
	vt_t wvt = calc_delta(gh->tick_length, p->he.weight);
	if (wvt > vt) {
		grp_add_vruntime(p, -(wvt-vt));
	}
}

// Yield and enqueue
void gh_yield(struct global_heap *gh, struct core *c, struct process *p, t_t time_passed) {
	grp_adjust_vruntime(gh, p, time_passed);

	struct heap *h = mh_choose_heap(p->mh, c);

	enq_proc_vt(gh, c, p, h);

	if(debug) {
		printf("%d(%d): yield time_passed %ld nt %d w %d vt %u\n", p->pid, p->group->gid, time_passed, p->group->nthread, p->he.weight, p->he.vruntime);
		mh_print(p->group->mh);
	}
}

// Process p is not runnable and yields core, which may make
// p's group not runnable
void gh_dequeue(struct global_heap *gh, struct core *c, struct process *p, t_t time_passed) {
	struct heap *h = p->h;
	lock_acquire(&h->lk);

	if(debug) {
		printf("%d(%d): dequeue %ld\n", p->pid, p->group->gid, time_passed);
		mh_print(p->group->mh);
	}

	grp_adjust_vruntime(gh, p, time_passed);

        int old_nthread = atomic_fetch_add(&p->group->nthread, -1);
	if (old_nthread == 1) {
		vt_t h_min = mh_min_vt(p->h);
		p->group->min_vt_deq = h_min;
		ticks_gettime(p->group->sleepstart);
	}

	p->h = NULL;

	lock_release(&h->lk);
}

void gh_print(struct global_heap *gh, struct group *grps[], int n) {
	mh_print(gh->mh);
	printf("= groups %d:\n", n);
	for(int i = 0; i < n; i++) {
		printf("  "); grp_print(grps[i]); printf("\n");
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
		printf("  "); grp_stats(grps[i], tot); printf("\n");
	}
}
