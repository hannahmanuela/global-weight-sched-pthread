#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "vt.h"
#include "group.h"
#include "heap.h"
#include "lheap.h"
#include "mheap.h"
#include "global_heap.h"
#include "util.h"

#define GRP2 2
#define GRP10 10
#define NPROC 2

int num_cores;
extern int tick_length;

void ticks_gettime(t_t *ticks) {
}

void ticks_getidle(t_t *ticks) {
}

void ticks_getwork(t_t *ticks) {
}

struct process *schedule_retry(int core, struct mheap *mh) {
	struct process *p;
	for (int i = 0; i < 10; i++) {
		p = schedule(0, mh);
		if(p != NULL)
			return p;
	}
	assert(0);
}

struct mheap *mk_mheap(int nheap, int ngrp, int nproc, struct group *gs[], int ws[]) {
	struct mheap *mh = mh_new(grp_cmp, nheap, 1);
	for (int i = 0; i < ngrp; i++) {
		gs[i] = grp_new(i, ws[i]);
		mh_add_group(mh, gs[i]);
		for (int j = 0; j < nproc; j++) {
			struct process *p = grp_new_process(j, gs[i]);
			enqueue(p);
		}
	}
	return mh;
}

void test_mheap(int nheap) {
	printf("test_%d_mheap start\n", nheap);

	struct group *gs[GRP2];
	int ws[GRP2] = {10, 20};
	struct mheap *mh = mk_mheap(nheap, GRP2, NPROC, gs, ws);
	struct process *p;

	// run the two groups to get off vt 0
	p = schedule_retry(0, mh);
	yield(p, tick_length);
	p = schedule_retry(0, mh);
	yield(p, tick_length);

	p = schedule_retry(0, mh);
	assert(p->group->group_id == GRP2-1);
	assert(p->group->vruntime == 100);
	yield(p, tick_length);
	p = schedule_retry(0, mh);
	assert(p->group->group_id == GRP2-1);
	assert(p->group->vruntime == 150);
	yield(p, tick_length);
	p = schedule_retry(0, mh);
	assert(p->group->group_id == 0);
	assert(p->group->vruntime == 200);
	yield(p, tick_length);
	printf("test_%d_mheap ok\n", nheap);
}

void test_mheap_many_grp(int nheap) {
	printf("test_%d_mheap grp %d\n", nheap, GRP10); 
	int n = 100000;
	struct group *gs[GRP10];
	int ticks[GRP10];
	int ws[GRP10];
	for (int i = 0; i < GRP10; i++) {
		ws[i] = (i+1)*5;
		ticks[i] = 0;
	}
	struct mheap *mh = mk_mheap(nheap, GRP10, NPROC, gs, ws);
	tick_length = 4000;
	for (int i = 0; i < n; i++) {
		struct process *p = schedule_retry(0, mh);
		yield(p, tick_length);
		ticks[p->group->group_id] += 1;
	}	
	for (int i = 1; i < GRP10; i++) {
		float w = (1.0 * ticks[i])/ticks[0];
		float m = 0.1;
		float l = (i+1)-m;
		float h = (i+1)+m; 
		printf("%0.2f %0.2f %0.2f\n", w, l, h);
		assert(w >= l && w <= h);
	}
	printf("test_%d_mheap grp %d: OK\n", nheap, GRP10); 
}

void test_worst(int nheap) {
	int n = 10000;
	int sum = 0;
	int seed = getpid();
	int worst;
	for(int t = 0; t < n; t++) {
		struct mheap *mh = mh_new(grp_cmp, nheap, seed+t);
		struct group *g = grp_new(0, 10);
		mh_add_group(mh, g);

		struct process *p = grp_new_process(1, g);
		enqueue(p);

		for (int i = 0; ; i++) {
			if ((p = schedule(0, mh)) != NULL) {
				sum += i;
				if(i > worst)
					worst = i;
				break;
			}
		}
	}
	printf("test_worst: avg %d worst %d\n", sum/n, worst);
}

void main(int argc, char *argv[]) {
	test_mheap(1);
	test_mheap(2);
	test_mheap_many_grp(1);
	test_mheap_many_grp(2);
	test_mheap_many_grp(5);
	test_worst(112);
}

