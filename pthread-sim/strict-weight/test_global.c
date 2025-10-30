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


void test_mheap(int nheap) {
	printf("test_%d_mheap start\n", nheap);

	struct mheap *mh = mh_new(grp_cmp, nheap, 1);
	struct group *gs[GRP2];
	int ws[GRP2] = {10, 20};
	struct process *p;
	
	for (int i = 0; i < GRP2; i++) {
		gs[i] = grp_new(i, ws[i]);
		mh_add_group(mh, gs[i]);
		for (int j = 0; j < NPROC; j++) {
			struct process *p = grp_new_process(j, gs[i]);
			enqueue(p);
		}

	}


	// run the two groups to get off vt 0
	p = schedule_retry(0, mh);
	yield(p, tick_length);
	p = schedule_retry(0, mh);
	yield(p, tick_length);

	p = schedule_retry(0, mh);
	assert(p != NULL);
	assert(p->group->group_id == GRP2-1);
	assert(p->group->vruntime == 100);
	p = schedule_retry(0, mh);
	assert(p != NULL);
	assert(p->group->group_id == GRP2-1);
	assert(p->group->vruntime == 150);
	p = schedule_retry(0, mh);
	assert(p != NULL);
	assert(p->group->group_id == 0);
	assert(p->group->vruntime == 200);
	printf("test_%d_mheap ok\n", nheap);
}

void test_mheap_many(int nheap) {
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
	test_worst(112);
}

