#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "vt.h"
#include "ticks.h"
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
#define PROC2 2
#define PROC1 1
#define PROC5 5

int num_cores;
extern bool debug;

void ticks_gettime(t_t *ticks) {
}

void ticks_getidle(t_t *ticks) {
}

void ticks_getwork(t_t *ticks) {
}

static struct process *schedule_retry(struct core *c, struct mheap *mh) {
	struct process *p;
	for (int i = 0; i < 10; i++) {
		p = schedule(c, mh);
		if(p != NULL)
			return p;
	}
	assert(0);
}

static struct mheap *mk_mheap(struct core *c, int nheap, int ngrp, int nproc, int tl, struct group **gs, int ws[]) {
	// struct mheap *mh = mh_new(proc_cmp, nheap, 1, tl);
	struct mheap *mh = mh_new(proc_cmp, nheap, tl);
	for (int i = 0; i < ngrp; i++) {
		gs[i] = grp_new(mh, i, ws[i]);
		for (int j = 0; j < nproc; j++) {
			struct process *p = grp_new_process(mh, i * nproc + j, gs[i]);
			enqueue(c, p);
		}
	}
	return mh;
}

static void cleanup(struct mheap *mh) {
	mh_free(mh);
}

void test_grp_sleep_wakeup() {
	printf("== test_sleep_wakeup start\n");

	int tl = 1000;
	struct group *gs[GRP1];
	int ws[GRP1] = {1};

	struct core *c = c_new(0);
	struct mheap *mh = mk_mheap(c, 1, GRP1, PROC2, tl, gs, ws);
	struct process *p0;
	struct process *p1;
		
	assert(gs[0]->vruntime == 2 * tl);

	p0 = schedule_retry(c, mh);
	p1 = schedule_retry(c, mh);
	dequeue(c, p1, tl);

	yield(c, p0, tl);
	assert(gs[0]->vruntime == 3 * tl);

	p0 = schedule_retry(c, mh);
	dequeue(c, p0, tl);

	assert(schedule(c, mh) == NULL);
	assert(mh->h[0]->heap_size == 1);

	enqueue(c, p0);

	assert(gs[0]->vruntime == 4*tl);

	assert(mh->h[0]->heap_size == 2);
	p0 = schedule_retry(c, mh);
	enqueue(c, p1);

	assert(gs[0]->vruntime == 5 * tl);

	yield(c, p0, tl);

	assert(gs[0]->vruntime == 6 * tl);

	p1 = schedule_retry(c, mh);

	yield(c, p1, tl/2);

	assert(gs[0]->vruntime == 6 * tl + tl/2);

	cleanup(mh);

	printf("-- test_sleep_wakeup ok\n");
}

void test_mheap_wakeup_lag() {
	printf("== test_wakeup_lag start\n");

	struct group *gs[GRP3];
	int ws[GRP3] = {10, 5, 1};

	int tl = 1000;

	int nheap = 1;
	struct core *c = c_new(0);
	struct mheap *mh = mk_mheap(c, 1, GRP3, PROC1, tl, gs, ws);

	// run the groups to get off vt 0
	for (int i = 0; i < GRP3; i++) {
		struct process *p = schedule_retry(c, mh);
		assert(p->he.vruntime == 0);
		yield(c, p, mh->tick_length);
	}

	struct process *p0 = schedule_retry(c, mh);
	assert(p0->pid == 0);

	struct process *p1 = schedule_retry(c, mh);
	assert(p1->pid == 1);

	dequeue(c, p0, tl);
	dequeue(c, p1, tl);

	enqueue(c, p1);
	printf("enq 1"); print(mh, gs, GRP3);
	assert(p1->he.vruntime == 400);

	enqueue(c, p0);
	printf("enq 0"); print(mh, gs, GRP3);
	assert(p0->he.vruntime == 200);
}


void test_mheap(int nheap, int nproc) {
	printf("== test_%d_mheap start np %d\n", nheap, nproc);

	struct group *gs[GRP2];
	int ws[GRP2] = {10, 20};
	int tl = 1000;
	struct core *c = c_new(0);
	struct mheap *mh = mk_mheap(c, nheap, GRP2, nproc, tl, gs, ws);
	struct process *p;

	// run the two groups to get off vt 0
	for (int i = 0; i < GRP2; i++) {
		p = schedule_retry(c, mh);
		assert(p->he.vruntime == 0);
		yield(c, p, mh->tick_length);
	}

	p = schedule_retry(c, mh);
	assert(p->group->gid == GRP2-1);
	assert(p->he.vruntime == 50);
	yield(c, p, mh->tick_length);
	p = schedule_retry(c, mh);
	assert(p->group->gid == GRP2-1);
	assert(p->he.vruntime == 100);
	yield(c, p, mh->tick_length);
	p = schedule_retry(c, mh);
	assert(p->group->gid == 0);
	assert(p->he.vruntime == 100);
	yield(c, p, mh->tick_length);

	// stats(gs, GRP2);

	cleanup(mh);

	printf("-- test_%d_mheap ok\n", nheap);
}

void test_mheap_many_grp(int nheap, int ngrp, int nproc, bool rand) {
	printf("== test_%d_mheap_grp r %d ng %d np %d\n", nheap, rand, ngrp, nproc); 
	int n = 100000;
	int tl = 4000;
	struct group **gs = malloc(sizeof(struct group *) * ngrp);
	int *ws = malloc(sizeof(int) * ngrp); 
	int *ticks = malloc(sizeof(int) * ngrp);
	int tot_w = 0;
	for (int i = 0; i < ngrp; i++) {
		ws[i] = (i+1)*5;
		tot_w += ws[i];
		ticks[i] = 0;
	}
	struct core *c = c_new(0);
	struct mheap *mh = mk_mheap(c, nheap, ngrp, nproc, tl, gs, ws);
	long tot = 0;
	for (int i = 0; i < n; i++) {
		struct process *p = schedule_retry(c, mh);
		int tl = mh->tick_length;
		if(rand) {
			tl = random() % mh->tick_length;
		}
		yield(c, p, tl);
		ticks[p->group->gid] += tl;
		tot += tl;
	}	
	for (int i = 0; i < ngrp; i++) {
		float e = ((1.0*ws[i])/tot_w)*tot;
		float g = e/ticks[i];
		// printf("ticks %d %0.2f %0.2f\n", ticks[i], e, g);
		assert(g >= 0.97 && g <= 1.03);
	}
	cleanup(mh);
	printf("-- test_%d_mheap_grp %d: OK\n", nheap, ngrp); 
}

void mheap_sleeper(struct core *c, struct mheap *mh, int n, int sleep_id, int ticks[], int sleep[], struct group *gs[], int ngrp) {
	struct process *sleeper = NULL;
	int sleeping = 0;
	for (int i = 0; i < n; i++) {
		if(sleeper != NULL) {
			sleep[sleeper->group->gid] += 1;
		}
		struct process *p = schedule_retry(c, mh);
		//printf("%d: p %d gid %d\n", i, p->pid, p->group->gid);
		if(p->group->gid != sleep_id) {
			yield(c, p, mh->tick_length);
			ticks[p->group->gid] += 1;
		} else if (sleeper == NULL) {
			//printf("%d: deque: %d\n", i, sleep_id, ticks[p->group->gid]);
			dequeue(c, p, mh->tick_length);
			//print(mh, gs, ngrp);
			ticks[p->group->gid] += 1;
			sleeping = i;
			sleeper = p;
		}
		if ((sleeper != NULL) && (i-sleeping > 4)) {
			//printf("%d: enque: %d\n", i, sleep_id);
			enqueue(c, sleeper);
			//print(mh, gs, ngrp);
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
	struct group **gs = malloc(sizeof(struct group *) *ngrp);
	int *ticks = malloc(sizeof(int) * ngrp);
	int *sleep = malloc(sizeof(int) * ngrp);
	int *ws = malloc(sizeof(int) * ngrp); 
	int tot_ws = 0;
	for(int i = 0; i < ngrp; i++) {
		ticks[i] = 0;
		sleep[i] = 0;
		ws[i] = 10*(i+1);
		tot_ws += ws[i];
	}
	struct core *c = c_new(0);
	struct mheap *mh = mk_mheap(c, nheap, ngrp, PROC1, tl, gs, ws);
	mheap_sleeper(c, mh, n, sleep_id, ticks, sleep, gs, ngrp);
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
	cleanup(mh);
	printf("-- test_%d_mheap_sleep grp %d: OK\n", nheap, ngrp); 
}

void test_worst(int nheap) {
	int n = 10000;
	int tl = 1000;
	int sum = 0;
	int worst;
	for(int t = 0; t < n; t++) {
		struct core *c = c_new(0);
		struct mheap *mh = mh_new(proc_cmp, nheap, tl);
		struct group *g = grp_new(mh, 0, 10);
		struct heap *h = mh_choose_heap(c, mh);

		struct process *p = grp_new_process(mh, 1, g);
		enqueue(c, p);

		for (int i = 0; ; i++) {
			if ((p = schedule(c, mh)) != NULL) {
				sum += i;
				if(i > worst)
					worst = i;
				break;
			}
		}
		cleanup(mh);
	}
	printf("== test_worst: avg %d worst %d\n", sum/n, worst);
}



void main(int argc, char *argv[]) {
	srandom(getpid());
	// debug = true;
	test_grp_sleep_wakeup();
	test_mheap_wakeup_lag();
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

