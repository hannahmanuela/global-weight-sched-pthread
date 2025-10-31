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
#define PROC2 2
#define PROC1 1

int num_cores;

void ticks_gettime(t_t *ticks) {
}

void ticks_getidle(t_t *ticks) {
}

void ticks_getwork(t_t *ticks) {
}

static struct process *schedule_retry(int core, struct mheap *mh) {
	struct process *p;
	for (int i = 0; i < 10; i++) {
		p = schedule(0, mh);
		if(p != NULL)
			return p;
	}
	assert(0);
}

static struct mheap *mk_mheap(int nheap, int ngrp, int nproc, int tl, struct group *gs[], int ws[]) {
	struct mheap *mh = mh_new(grp_cmp, nheap, 1, tl);
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
	int tl = 1000;
	struct mheap *mh = mk_mheap(nheap, GRP2, PROC2, tl, gs, ws);
	struct process *p;

	mh_print(mh);
	// run the two groups to get off vt 0
	p = schedule_retry(0, mh);
	yield(p, mh->tick_length);
	p = schedule_retry(0, mh);
	yield(p, mh->tick_length);

	mh_print(mh);
	p = schedule_retry(0, mh);
	assert(p->group->group_id == GRP2-1);
	assert(p->group->vruntime == 100);
	yield(p, mh->tick_length);
	p = schedule_retry(0, mh);
	assert(p->group->group_id == GRP2-1);
	assert(p->group->vruntime == 150);
	yield(p, mh->tick_length);
	p = schedule_retry(0, mh);
	assert(p->group->group_id == 0);
	assert(p->group->vruntime == 200);
	yield(p, mh->tick_length);
	printf("test_%d_mheap ok\n", nheap);
}

void test_mheap_many_grp(int nheap) {
	printf("test_%d_mheap grp %d\n", nheap, GRP10); 
	int n = 100000;
	int tl = 4000;
	struct group *gs[GRP10];
	int ticks[GRP10];
	int ws[GRP10];
	for (int i = 0; i < GRP10; i++) {
		ws[i] = (i+1)*5;
		ticks[i] = 0;
	}
	struct mheap *mh = mk_mheap(nheap, GRP10, PROC2, tl, gs, ws);
	for (int i = 0; i < n; i++) {
		struct process *p = schedule_retry(0, mh);
		yield(p, mh->tick_length);
		ticks[p->group->group_id] += 1;
	}	
	for (int i = 1; i < GRP10; i++) {
		float w = (1.0 * ticks[i])/ticks[0];
		float m = 0.1;
		float l = (i+1)-m;
		float h = (i+1)+m; 
		// printf("%0.2f %0.2f %0.2f\n", w, l, h);
		assert(w >= l && w <= h);
	}
	printf("test_%d_mheap grp %d: OK\n", nheap, GRP10); 
}

void mheap_sleeper(struct mheap *mh, int n, int sleep_id, int ticks[], int sleep[]) {
	struct process *sleeper = NULL;
	int sleeping = 0;
	for (int i = 0; i < n; i++) {
		if(sleeper != NULL) {
			sleep[sleeper->group->group_id] += 1;
		}
		struct process *p = schedule_retry(0, mh);
		//printf("%d: p gid %d\n", i, p->group->group_id);
		if(p->group->group_id != sleep_id) {
			yield(p, mh->tick_length);
			ticks[p->group->group_id] += 1;
		} else if (sleeper == NULL) {
			//printf("%d: deque: %d\n", i, sleep_id, ticks[p->group->group_id]);
			dequeue(p, mh->tick_length);
			ticks[p->group->group_id] += 1;
			sleeping = i;
			sleeper = p;
		}
		if ((sleeper != NULL) && (i-sleeping > 4)) {
			//printf("%d: enque: %d\n", i, sleep_id);
			enqueue(sleeper);
			//mh_print(mh);
			sleeping = 0;
			sleeper = NULL;
		}
	}	
}

void test_mheap_sleep(int nheap, int sleep_id) {
	printf("test_%d_mheap_sleep %d grp %d\n", nheap, sleep_id, GRP2); 
	int n = 100000;
	// int n = 20;
	int tl = 1000;
	struct group *gs[GRP2];
	int ticks[GRP2] = {0,0};
	int sleep[GRP2] = {0,0};
	int ws[GRP2] = {10, 20};

	struct mheap *mh = mk_mheap(nheap, GRP2, PROC1, tl, gs, ws);
	mheap_sleeper(mh, n, sleep_id, ticks, sleep);

	float f = 1.0*ticks[sleep_id]/(n-sleep[sleep_id]);
	float g = 1.0 * ws[sleep_id] /(10+20);
	float m = 0.1;
        printf("ticks %d sleep %d %0.2f g %0.2f\n", ticks[sleep_id], sleep[sleep_id], f, g);
	assert(f >= (g - m) && f < (g+m));
	printf("test_%d_mheap_sleep grp %d: OK\n", nheap, GRP2); 
}

void test_worst(int nheap) {
	int n = 10000;
	int tl = 1000;
	int sum = 0;
	int seed = getpid();
	int worst;
	for(int t = 0; t < n; t++) {
		struct mheap *mh = mh_new(grp_cmp, nheap, seed+t, tl);
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
	test_mheap_sleep(1, 0);
	test_mheap_sleep(1, 1);
	test_worst(112);
}

