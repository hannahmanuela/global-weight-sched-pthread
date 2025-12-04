#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "vt.h"
#include "core.h"
#include "group.h"
#include "heap.h"
#include "mheap.h"
#include "global_heap.h"
#include "util.h"

#define GRP1 1
#define GRP2 2
#define GRP3 3
#define GRP10 10
#define PROC1 1
#define PROC2 2
#define PROC3 3
#define PROC5 5

int num_cores;
extern bool debug;

void ticks_gettime(t_t *ticks) {
}

void ticks_getidle(t_t *ticks) {
}

void ticks_getwork(t_t *ticks) {
}

static struct process *schedule_retry(struct core *c, struct global_heap *gh) {
	struct process *p;
	for (int i = 0; i < 10; i++) {
		p = gh_schedule(gh, c);
		if(p != NULL)
			return p;
	}
	assert(0);
}

static struct global_heap *mk_mheap(struct core *c, int nheap, int ngrp, int nproc, int tl, struct group **gs, int ws[]) {
	struct global_heap *gh = gh_new(tl, nheap);
	for (int i = 0; i < ngrp; i++) {
		gs[i] = grp_new(gh->mh, i, ws[i]);
		for (int j = 0; j < nproc; j++) {
			struct process *p = grp_new_process(gh->mh, i * nproc + j, gs[i]);
			gh_enqueue(gh, c, p);
		}
	}
	return gh;
}

static void cleanup(struct mheap *mh) {
	mh_free(mh);
}

void test_grp_sleep_wakeup() {
	printf("== test_sleep_wakeup start\n");

	int tl = 1000;
	struct group *gs[GRP1];
	int ws[GRP1] = {1};

	struct core *c = c_new(0, GRP1);
	struct global_heap *gh = mk_mheap(c, 1, GRP1, PROC2, tl, gs, ws);
	struct process *p0;
	struct process *p1;
		
	assert(gs[0]->vruntime == 2 * tl);

	p0 = schedule_retry(c, gh);
	p1 = schedule_retry(c, gh);
	gh_dequeue(gh, c, p1, tl);

	gh_yield(gh, c, p0, tl);
	assert(gs[0]->vruntime == 3 * tl);

	p0 = schedule_retry(c, gh);
	gh_dequeue(gh, c, p0, tl);

	assert(gh_schedule(gh, c) == NULL);
	assert(gh->mh->h[0]->heap_size == 1);

	gh_enqueue(gh, c, p0);

	assert(gs[0]->vruntime == 4*tl);

	assert(gh->mh->h[0]->heap_size == 2);
	p0 = schedule_retry(c, gh);
	gh_enqueue(gh, c, p1);

	assert(gs[0]->vruntime == 5 * tl);

	gh_yield(gh, c, p0, tl);

	assert(gs[0]->vruntime == 6 * tl);

	p1 = schedule_retry(c, gh);

	gh_yield(gh, c, p1, tl/2);

	assert(gs[0]->vruntime == 6 * tl + tl/2);

	cleanup(gh->mh);

	printf("-- test_sleep_wakeup ok\n");
}

void test_grp_fair_lag() {
	printf("== test_grp_fair_lag start\n");

	struct group *gs[GRP1];
	int ws[GRP1] = {10};

	int tl = 1000;

	int nheap = 1;
	struct core *c = c_new(0, GRP1);
	struct global_heap *gh = mk_mheap(c, 1, GRP1, PROC3, tl, gs, ws);

	// run the groups to get off vt 0
	for (int i = 0; i < GRP1; i++) {
		struct process *p = schedule_retry(c, gh);
		assert(p->he.vruntime == 0);
		gh_yield(gh, c, p, gh->tick_length);
	}

        gh_print(gh, gs, GRP1);

	struct process *p0 = schedule_retry(c, gh);
	struct process *p1 = schedule_retry(c, gh);

	gh_yield(gh, c, p0, tl/10);

	gh_yield(gh, c, p1, tl/10);

        gh_print(gh, gs, GRP1);
	assert(p0->he.vruntime > 300);
}

void test_grp_fair_sleep_lag() {
	printf("== test_grp_fair_sleep_lag start\n");

	struct group *gs[GRP1];
	int ws[GRP1] = {10};

	int tl = 1000;

	int nheap = 1;
	struct core *c = c_new(0, GRP1);
	struct global_heap *gh = mk_mheap(c, 1, GRP1, PROC3, tl, gs, ws);

	// run the groups to get off vt 0
	for (int i = 0; i < GRP1; i++) {
		struct process *p = schedule_retry(c, gh);
		assert(p->he.vruntime == 0);
		gh_yield(gh, c, p, gh->tick_length);
	}

        gh_print(gh, gs, GRP1);

	struct process *p0 = schedule_retry(c, gh);
	struct process *p1 = schedule_retry(c, gh);

	gh_dequeue(gh, c, p0, tl/10);

	gh_dequeue(gh, c, p1, tl/10);

	gh_enqueue(gh, c, p0);
	gh_enqueue(gh, c, p1);

        gh_print(gh, gs, GRP1);
	assert(p0->he.vruntime > 300);
}

void test_mheap_wakeup_lag() {
	printf("== test_wakeup_lag start\n");

	struct group *gs[GRP3];
	int ws[GRP3] = {10, 5, 1};

	int tl = 1000;

	int nheap = 1;
	struct core *c = c_new(0, GRP3);
	struct global_heap *gh = mk_mheap(c, 1, GRP3, PROC1, tl, gs, ws);

	// run the groups to get off vt 0
	for (int i = 0; i < GRP3; i++) {
		struct process *p = schedule_retry(c, gh);
		assert(p->he.vruntime == 0);
		gh_yield(gh, c, p, gh->tick_length);
	}

	struct process *p0 = schedule_retry(c, gh);
	assert(p0->pid == 0);

	struct process *p1 = schedule_retry(c, gh);
	assert(p1->pid == 1);

	gh_dequeue(gh, c, p0, tl);
	gh_dequeue(gh, c, p1, tl);

	gh_enqueue(gh, c, p1);
	assert(p1->he.vruntime == 400);

	gh_enqueue(gh, c, p0);
	assert(p0->he.vruntime == 200);
}

void test_mheap_fair_lag() {
	printf("== test_fair_lag start\n");

	struct group *gs[GRP3];
	int ws[GRP3] = {10, 5, 1};

	int tl = 1000;

	int nheap = 1;
	struct core *c = c_new(0, GRP3);
	struct global_heap *gh = mk_mheap(c, 1, GRP3, PROC1, tl, gs, ws);

	// run the groups to get off vt 0
	for (int i = 0; i < GRP3; i++) {
		struct process *p = schedule_retry(c, gh);
		assert(p->he.vruntime == 0);
		gh_yield(gh, c, p, gh->tick_length);
	}

	struct process *p0 = schedule_retry(c, gh);
	assert(p0->pid == 0);

	struct process *p1 = schedule_retry(c, gh);
	assert(p1->pid == 1);

	gh_dequeue(gh, c, p0, tl);
	gh_dequeue(gh, c, p1, tl);

	gh_enqueue(gh, c, p1);
	assert(p1->he.vruntime == 400);

	gh_enqueue(gh, c, p0);
	assert(p0->he.vruntime == 200);
}


void test_mheap(int nheap, int nproc) {
	printf("== test_%d_mheap start np %d\n", nheap, nproc);

	struct group *gs[GRP2];
	int ws[GRP2] = {10, 20};
	int tl = 1000;
	struct core *c = c_new(0, GRP2);
	struct global_heap *gh = mk_mheap(c, nheap, GRP2, nproc, tl, gs, ws);
	struct process *p;

	// run the two groups to get off vt 0
	for (int i = 0; i < GRP2; i++) {
		p = schedule_retry(c, gh);
		assert(p->he.vruntime == 0);
		gh_yield(gh, c, p, gh->tick_length);
	}

	p = schedule_retry(c, gh);
	assert(p->group->gid == GRP2-1);
	assert(p->he.vruntime == 50);
	gh_yield(gh, c, p, gh->tick_length);
	p = schedule_retry(c, gh);
	assert(p->group->gid == GRP2-1);
	assert(p->he.vruntime == 100);
	gh_yield(gh, c, p, gh->tick_length);
	p = schedule_retry(c, gh);
	assert(p->group->gid == 0);
	assert(p->he.vruntime == 100);
	gh_yield(gh, c, p, gh->tick_length);

	// stats(gs, GRP2);

	cleanup(gh->mh);

	printf("-- test_%d_mheap ok\n", nheap);
}

void test_mheap_many_grp(int nheap, int ngrp, int nproc, bool rand) {
	printf("== test_%d_mheap_grp r %d ng %d np %d\n", nheap, rand, ngrp, nproc); 
	int n = 100000;
	int tl = 4000;
	struct group **gs = calloc(ngrp, sizeof(struct group *));
	int *ws = calloc(ngrp, sizeof(int));
	int *ticks = calloc(ngrp, sizeof(int));
	int tot_w = 0;
	for (int i = 0; i < ngrp; i++) {
		ws[i] = (i+1)*5;
		tot_w += ws[i];
	}
	struct core *c = c_new(0, ngrp);
	struct global_heap *gh = mk_mheap(c, nheap, ngrp, nproc, tl, gs, ws);
	long tot = 0;
	for (int i = 0; i < n; i++) {
		struct process *p = schedule_retry(c, gh);
		int tl = gh->tick_length;
		if(rand) {
			tl = random() % gh->tick_length;
		}
		gh_yield(gh, c, p, tl);
		ticks[p->group->gid] += tl;
		tot += tl;
	}	
	for (int i = 0; i < ngrp; i++) {
		float e = ((1.0*ws[i])/tot_w)*tot;
		float g = e/ticks[i];
		// printf("ticks %d %0.2f %0.2f\n", ticks[i], e, g);
		assert(g >= 0.97 && g <= 1.03);
	}
	cleanup(gh->mh);
	printf("-- test_%d_mheap_grp %d: OK\n", nheap, ngrp); 
}

void mheap_sleeper(struct core *c, struct global_heap *gh, int n, int sleep_id, int ticks[], int sleep[], struct group *gs[], int ngrp) {
	struct process *sleeper = NULL;
	int sleeping = 0;
	for (int i = 0; i < n; i++) {
		if(sleeper != NULL) {
			sleep[sleeper->group->gid] += 1;
		}
		struct process *p = schedule_retry(c, gh);
		//printf("%d: p %d gid %d\n", i, p->pid, p->group->gid);
		if(p->group->gid != sleep_id) {
			gh_yield(gh, c, p, gh->tick_length);
			ticks[p->group->gid] += 1;
		} else if (sleeper == NULL) {
			//printf("%d: deque: %d\n", i, sleep_id, ticks[p->group->gid]);
			gh_dequeue(gh, c, p, gh->tick_length);
			//print(gh->mh, gs, ngrp);
			ticks[p->group->gid] += 1;
			sleeping = i;
			sleeper = p;
		}
		if ((sleeper != NULL) && (i-sleeping > 4)) {
			//printf("%d: enque: %d\n", i, sleep_id);
			gh_enqueue(gh, c, sleeper);
			//print(gh->mh, gs, ngrp);
			sleeping = 0;
			sleeper = NULL;
		}
	}	
}

void test_mheap_sleep(int nheap, int sleep_id, int ngrp) {
	printf("== test_%d_mheap_sleep %d grp %d\n", nheap, sleep_id, ngrp); 
	int n = 100000;
	// int n = 20;
	int tl = 1000;
	struct group **gs = calloc(ngrp, sizeof(struct group *));
	int *ticks = calloc(ngrp, sizeof(int));
	int *sleep = calloc(ngrp, sizeof(int));
	int *ws = calloc(ngrp, sizeof(int));
	int tot_ws = 0;
	for(int i = 0; i < ngrp; i++) {
		ws[i] = 10*(i+1);
		tot_ws += ws[i];
	}
	struct core *c = c_new(0, ngrp);
	struct global_heap *gh = mk_mheap(c, nheap, ngrp, PROC1, tl, gs, ws);
	mheap_sleeper(c, gh, n, sleep_id, ticks, sleep, gs, ngrp);
	for (int i = 0; i < ngrp; i++) {
		if (i == sleep_id) {
			float f = AVG(ticks[sleep_id],(n-sleep[sleep_id]));
			float g = AVG(ws[sleep_id], tot_ws);
			float m = 0.1;
			printf("ticks %d sleep %d %0.2f g %0.2f\n", ticks[sleep_id], sleep[sleep_id], f, g);
			assert(f >= (g - m) && f < (g+m));
		} else {
			float f = AVG(ticks[i], n);
			float g = AVG(ws[i], tot_ws);
			float m = 0.1;
			printf("ticks %d sleep %d %0.2f g %0.2f\n", ticks[i], sleep[i], f, g);
			assert(f >= g);
		}
	}
	cleanup(gh->mh);
	printf("-- test_%d_mheap_sleep grp %d: OK\n", nheap, ngrp); 
}

void test_worst(int nheap) {
	int n = 10000;
	int tl = 1000;
	int sum = 0;
	int worst;
	for(int t = 0; t < n; t++) {
		struct core *c = c_new(0, GRP1);
		struct global_heap *gh = gh_new(tl, nheap);
		struct group *g = grp_new(gh->mh, 0, 10);
		struct heap *h = mh_choose_heap(gh->mh, c);

		struct process *p = grp_new_process(gh->mh, 1, g);
		gh_enqueue(gh, c, p);

		for (int i = 0; ; i++) {
			if ((p = gh_schedule(gh, c)) != NULL) {
				sum += i;
				if(i > worst)
					worst = i;
				break;
			}
		}
		cleanup(gh->mh);
	}
	printf("== test_worst: avg %d worst %d\n", sum/n, worst);
}

void test_running_lag() {
	printf("== test_running_lag start\n");

	struct group *gs[GRP2];
	int ws[GRP2] = {10, 5};

	int tl = 1000;
	int ngrp = GRP2;

	int nheap = 1;
	struct core *c = c_new(0, GRP2);
	struct global_heap *gh = gh_new(tl, nheap);
	for (int i = 0; i < ngrp; i++) {
		gs[i] = grp_new(gh->mh, i, ws[i]);
	}

	struct process *p1 = grp_new_process(gh->mh, 0, gs[0]);
	struct process *p2 = grp_new_process(gh->mh, 1, gs[1]);

	gh_enqueue(gh, c, p1);

	// run a bunch
	for (int i=0; i < 10; i++) {
		struct process *p = schedule_retry(c, gh);
		gh_yield(gh, c, p, gh->tick_length);
	}
	assert(p1->he.vruntime == 1000);

	struct process *p = schedule_retry(c, gh);
	gh_enqueue(gh, c, p2);

	// 1000 since p2 hasn't run yet; if it had run and dequeued,
	// then dequeue would make it 1100.
	assert(p2->he.vruntime == 1000);
}

void main(int argc, char *argv[]) {
	srandom(getpid());
	test_grp_fair_lag();
	test_grp_fair_sleep_lag();
	exit(1);
	// debug = true;
	test_grp_sleep_wakeup();
	test_grp_fair_lag();
	test_grp_fair_sleep_lag();
	test_mheap_wakeup_lag();
	test_running_lag();
	test_mheap(1, PROC1);
	test_mheap(1, PROC2);
	test_mheap(2, PROC1);
	test_mheap_many_grp(1, GRP10, PROC2, 0);
	test_mheap_many_grp(2, GRP10, PROC2, 0);
	test_mheap_many_grp(5, GRP10, PROC2, 0);
	test_mheap_many_grp(1, GRP10, PROC2, 1);
	test_mheap_many_grp(5, GRP10, PROC2, 1);
	test_mheap_sleep(1, 0, GRP2);
	test_mheap_sleep(1, 1, GRP2);
	test_mheap_sleep(1, 2, 3);
	test_worst(112);
}

