#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "vt.h"
#include "core.h"
#include "group.h"
#include "heap.h"
#include "mheap.h"
#include "sched_state.h"
#include "scheduler.h"
#include "util.h"

#define GRP1 1
#define GRP2 2
#define GRP3 3
#define GRP10 10
#define PROC1 1
#define PROC2 2
#define PROC3 3
#define PROC5 5
#define NCORE1 1
#define NCORE2 2 

int num_cores;
extern bool debug;
extern int scheduler;

void ticks_gettime(t_t *ticks) {
}

void ticks_getidle(t_t *ticks) {
}

void ticks_getwork(t_t *ticks) {
}

static struct process *schedule_retry(struct core *c, struct sched_state *ss) {
	struct process *p;
	for (int i = 0; i < 10; i++) {
		if (ss_schedule(ss, c))
			return c->process;
	}
	assert(0);
}

static struct sched_state *mk_mheap(struct core *cs[], int ncore, int nheap, int ngrp, int nproc, int tl, struct group **gs, int ws[]) {
	scheduler = GWFS;
	struct sched_state *ss = ss_new(tl, nheap, cs, ncore);
	for (int i = 0; i < ngrp; i++) {
		gs[i] = grp_new(ss->mh, i, ws[i]);
		for (int j = 0; j < nproc; j++) {
			struct process *p = grp_new_process(ss->mh, i * nproc + j, gs[i]);
			ss_enqueue(ss, cs[0], p);
		}
	}
	return ss;
}

static void cleanup(struct mheap *mh) {
	mh_free(mh);
}


void test_preempt_t() {
	preempt_t pre;

	pre = PREEMPT(10, 1, 2);
	assert(NCORE(pre) == 10);
	assert(WEIGHT(pre) == 1);
	assert(CORE(pre) == 2);
}

void test_load() {
	struct core *c[NCORE1] = {c_new(0, GRP1, getpid())};
	struct group *gs[GRP1];
	int ws[GRP1] = {1};
	int nheap = 8;
	int ntrial = 100;
	
	extern bool use_power2_insert;
	
	use_power2_insert = true;
	for (int nproc = 2; nproc < 2029; nproc += nproc) {
		int max = 0;
		float a = 0.0;
		for (int t = 0; t < ntrial; t++) {
			struct sched_state *ss = mk_mheap(c, NCORE1, nheap, GRP1, PROC2, 0, gs, ws);
			for (int i = 0; i < nproc; i++) {
				struct process *p = grp_new_process(ss->mh, i, gs[0]);
				struct heap *h = mh_choose_heap(ss->mh, c[0]);
				p->he.vruntime = safe_read_tsc();
				mh_add_process(c[0], p, h);
			}
			int maxl = 0;
			float avg = mh_load(ss->mh, &maxl);
			if (maxl > max)
				max = maxl;
			a = avg;
		}
		printf("n: %d avg %0.2f max %d max load %d\n", nproc, a, max, max- (int) a);
	}
}

void test_grp_sleep_wakeup() {
	printf("== test_sleep_wakeup start\n");

	int tl = 1000;
	struct group *gs[GRP1];
	int ws[GRP1] = {1};

	struct core *c[NCORE2] = {c_new(0, GRP1, 0), c_new(0, GRP1, 0)};
	struct sched_state *ss = mk_mheap(c, NCORE1, 1, GRP1, PROC2, tl, gs, ws);
	struct process *p0;
	struct process *p1;

	assert(gs[0]->vruntime == 2 * tl);

	p0 = schedule_retry(c[0], ss);
	p1 = schedule_retry(c[1], ss);
	ss_dequeue(ss, c[1], p1, tl);

	ss_yield(ss, c[0], p0, tl);
	
	assert(gs[0]->vruntime == 3 * tl);

	p0 = schedule_retry(c[0], ss);

	ss_dequeue(ss, c[0], p0, tl);

	assert(!ss_schedule(ss, c[0]));
	assert(ss->mh->h[0]->heap_size == 1);

	ss_enqueue(ss, c[0], p0);

	assert(gs[0]->vruntime == 4*tl);

	assert(ss->mh->h[0]->heap_size == 2);
	p0 = schedule_retry(c[0], ss);
	ss_enqueue(ss, c[1], p1);

	assert(gs[0]->vruntime == 5 * tl);

	ss_yield(ss, c[0], p0, tl);

	assert(gs[0]->vruntime == 6 * tl);

	p1 = schedule_retry(c[0], ss);

	ss_yield(ss, c[0], p1, tl/2);

	assert(gs[0]->vruntime == 6 * tl + tl/2);

	cleanup(ss->mh);

	printf("-- test_sleep_wakeup ok\n");
}

void test_grp_fair_lag() {
	printf("== test_grp_fair_lag start\n");

	struct group *gs[GRP1];
	int ws[GRP1] = {10};

	int tl = 1000;

	int nheap = 1;
	struct core *c[NCORE2] = {c_new(0, GRP1, 0), c_new(0, GRP1, 0)};
	struct sched_state *ss = mk_mheap(c, NCORE1, 1, GRP1, PROC3, tl, gs, ws);

	// run the groups to get off vt 0
	for (int i = 0; i < GRP1; i++) {
		struct process *p = schedule_retry(c[0], ss);
		assert(p->he.vruntime == 0);
		ss_yield(ss, c[0], p, ss->tick_length);
	}

        ss_print(ss, gs, GRP1);

	struct process *p0 = schedule_retry(c[0], ss);
	struct process *p1 = schedule_retry(c[1], ss);

	ss_yield(ss, c[0], p0, tl/10);

	ss_yield(ss, c[1], p1, tl/10);

        ss_print(ss, gs, GRP1);
	assert(p0->he.vruntime > 300);
}

void test_grp_fair_sleep_lag() {
	printf("== test_grp_fair_sleep_lag start\n");

	struct group *gs[GRP1];
	int ws[GRP1] = {10};

	int tl = 1000;

	int nheap = 1;
	struct core *c[NCORE2] = {c_new(0, GRP1, 0), c_new(0, GRP1, 0)};
	struct sched_state *ss = mk_mheap(c, NCORE1, 1, GRP1, PROC3, tl, gs, ws);

	// run the groups to get off vt 0
	for (int i = 0; i < GRP1; i++) {
		struct process *p = schedule_retry(c[0], ss);
		assert(p->he.vruntime == 0);
		ss_yield(ss, c[0], p, ss->tick_length);
	}

	struct process *p0 = schedule_retry(c[0], ss);
	struct process *p1 = schedule_retry(c[1], ss);

	ss_dequeue(ss, c[0], p0, tl/10);
	ss_dequeue(ss, c[1], p1, tl/10);


	ss_enqueue(ss, c[0], p0);
	ss_enqueue(ss, c[0], p1);

        // ss_print(ss, gs, GRP1);

	assert(p0->he.vruntime == 300);
	assert(p1->he.vruntime == 320);
}

void test_mheap_wakeup_lag() {
	printf("== test_wakeup_lag start\n");

	struct group *gs[GRP3];
	int ws[GRP3] = {10, 5, 1};

	int tl = 1000;

	int nheap = 1;
	struct core *c[NCORE2] = {c_new(0, GRP1, 0), c_new(0, GRP1, 0)};
	struct sched_state *ss = mk_mheap(c, NCORE1, 1, GRP3, PROC1, tl, gs, ws);

	// run the groups to get off vt 0
	for (int i = 0; i < GRP3; i++) {
		struct process *p = schedule_retry(c[0], ss);
		assert(p->he.vruntime == 0);
		ss_yield(ss, c[0], p, ss->tick_length);
	}

	struct process *p0 = schedule_retry(c[0], ss);
	assert(p0->pid == 0);

	struct process *p1 = schedule_retry(c[1], ss);
	assert(p1->pid == 1);

	ss_dequeue(ss, c[0], p0, tl);
	ss_dequeue(ss, c[1], p1, tl);

	ss_enqueue(ss, c[0], p1);
	assert(p1->he.vruntime == 400);

	ss_enqueue(ss, c[0], p0);
	assert(p0->he.vruntime == 200);
}

void test_mheap_fair_lag() {
	printf("== test_fair_lag start\n");

	struct group *gs[GRP3];
	int ws[GRP3] = {10, 5, 1};

	int tl = 1000;

	int nheap = 1;
	struct core *c[NCORE1] = {c_new(0, GRP3, 0)};
	struct sched_state *ss = mk_mheap(c, NCORE1, 1, GRP3, PROC1, tl, gs, ws);

	// run the groups to get off vt 0
	for (int i = 0; i < GRP3; i++) {
		struct process *p = schedule_retry(c[0], ss);
		assert(p->he.vruntime == 0);
		ss_yield(ss, c[0], p, ss->tick_length);
	}

	struct process *p0 = schedule_retry(c[0], ss);
	assert(p0->pid == 0);

	struct process *p1 = schedule_retry(c[0], ss);
	assert(p1->pid == 1);

	ss_dequeue(ss, c[0], p0, tl);
	ss_dequeue(ss, c[0], p1, tl);

	ss_enqueue(ss, c[0], p1);
	assert(p1->he.vruntime == 400);

	ss_enqueue(ss, c[0], p0);
	assert(p0->he.vruntime == 200);
}

void test_running_lag() {
	printf("== test_running_lag start\n");

	struct group *gs[GRP2];
	int ws[GRP2] = {10, 5};

	int tl = 1000;
	int ngrp = GRP2;

	int nheap = 1;
	struct core *c[NCORE1] = {c_new(0, GRP2, 0)};
	// struct core *c[NCORE2] = {c_new(0, GRP1, 0), c_new(0, GRP1, 0)};

	struct sched_state *ss = ss_new(tl, nheap, c, num_cores);
	for (int i = 0; i < ngrp; i++) {
		gs[i] = grp_new(ss->mh, i, ws[i]);
	}

	struct process *p1 = grp_new_process(ss->mh, 0, gs[0]);
	struct process *p2 = grp_new_process(ss->mh, 1, gs[1]);

	ss_enqueue(ss, c[0], p1);

	// run a bunch
	for (int i=0; i < 10; i++) {
		struct process *p = schedule_retry(c[0], ss);
		ss_yield(ss, c[0], p, ss->tick_length);
	}
	assert(p1->he.vruntime == 1000);

	struct process *p = schedule_retry(c[0], ss);

	ss_enqueue(ss, c[0], p2);

	// 1000 since p2 hasn't run yet; if it had run and dequeued,
	// then dequeue would make it 1100.
	assert(p2->he.vruntime == 1000);
}

void test_preempt() {
	printf("== test_preempt\n");

	int nproc = 2;
	int nheap = 1;
	int tl = 1000;
	struct group *gs[GRP2];
	int ws[GRP2] = {1, 100};
	// allocate many cores
	struct core *c[NCORE2] = { c_new(0, GRP2, 0),  c_new(1, GRP2, 1) };
	struct sched_state *ss = mk_mheap(c, NCORE2, nheap, GRP2, nproc, tl, gs, ws);
	struct process *p;

	// run the two groups to get off vt 0
	for (int i = 0; i < GRP2; i++) {
		p = schedule_retry(c[0], ss);
		ss_yield(ss, c[0], p, ss->tick_length);
	}

	struct process *p1 = schedule_retry(c[0], ss);
	ss_dequeue(ss, c[0], p1, ss->tick_length/2);

	struct process *p2 = schedule_retry(c[0], ss);
	struct process *p3 = schedule_retry(c[1], ss);

	ss_print(ss, gs, GRP2);

	ss_enqueue(ss, c[0], p1);

	// measure kick
	// check the process running on that core

}

void test_mheap(int nheap, int nproc) {
	printf("== test_%d_mheap start np %d\n", nheap, nproc);

	struct group *gs[GRP2];
	int ws[GRP2] = {10, 20};
	int tl = 1000;
	struct core *c[NCORE1] = {c_new(0, GRP2, 0)};
	struct sched_state *ss = mk_mheap(c, NCORE1, nheap, GRP2, nproc, tl, gs, ws);
	struct process *p;

	// run the two groups to get off vt 0
	for (int i = 0; i < GRP2; i++) {
		p = schedule_retry(c[0], ss);
		assert(p->he.vruntime == 0);
		ss_yield(ss, c[0], p, ss->tick_length);
	}

	p = schedule_retry(c[0], ss);
	assert(p->group->gid == GRP2-1);
	assert(p->he.vruntime == 50);

	ss_yield(ss, c[0], p, ss->tick_length);
	p = schedule_retry(c[0], ss);
	assert(p->group->gid == GRP2-1);
	assert(p->he.vruntime == 100);

	ss_yield(ss, c[0], p, ss->tick_length);
	p = schedule_retry(c[0], ss);
	assert(p->group->gid == 0);
	assert(p->he.vruntime == 100);
	ss_yield(ss, c[0], p, ss->tick_length);

	// stats(gs, GRP2);

	cleanup(ss->mh);

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
	struct core *c[NCORE1] = {c_new(0, ngrp, 0)};
	struct sched_state *ss = mk_mheap(c, NCORE1, nheap, ngrp, nproc, tl, gs, ws);
	long tot = 0;
	for (int i = 0; i < n; i++) {
		struct process *p = schedule_retry(c[0], ss);
		int tl = ss->tick_length;
		if(rand) {
			tl = random() % ss->tick_length;
		}
		ss_yield(ss, c[0], p, tl);
		ticks[p->group->gid] += tl;
		tot += tl;
	}	
	for (int i = 0; i < ngrp; i++) {
		float e = ((1.0*ws[i])/tot_w)*tot;
		float g = e/ticks[i];
		// printf("ticks %d %0.2f %0.2f\n", ticks[i], e, g);
		assert(g >= 0.97 && g <= 1.03);
	}
	cleanup(ss->mh);
	printf("-- test_%d_mheap_grp %d: OK\n", nheap, ngrp); 
}

void mheap_sleeper(struct core *c, struct sched_state *ss, int n, int sleep_id, int ticks[], int sleep[], struct group *gs[], int ngrp) {
	struct process *sleeper = NULL;
	int sleeping = 0;
	for (int i = 0; i < n; i++) {
		if(sleeper != NULL) {
			sleep[sleeper->group->gid] += 1;
		}
		struct process *p = schedule_retry(c, ss);
		//printf("%d: p %d gid %d\n", i, p->pid, p->group->gid);
		if(p->group->gid != sleep_id) {
			ss_yield(ss, c, p, ss->tick_length);
			ticks[p->group->gid] += 1;
		} else if (sleeper == NULL) {
			//printf("%d: deque: %d\n", i, sleep_id, ticks[p->group->gid]);
			ss_dequeue(ss, c, p, ss->tick_length);
			//print(ss->mh, gs, ngrp);
			ticks[p->group->gid] += 1;
			sleeping = i;
			sleeper = p;
		}
		if ((sleeper != NULL) && (i-sleeping > 4)) {
			//printf("%d: enque: %d\n", i, sleep_id);
			ss_enqueue(ss, c, sleeper);
			//print(ss->mh, gs, ngrp);
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
	struct core *c[NCORE1] = {c_new(0, ngrp, 0)};
	struct sched_state *ss = mk_mheap(c, NCORE1, nheap, ngrp, PROC1, tl, gs, ws);
	mheap_sleeper(c[0], ss, n, sleep_id, ticks, sleep, gs, ngrp);
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
	cleanup(ss->mh);
	printf("-- test_%d_mheap_sleep grp %d: OK\n", nheap, ngrp); 
}

void test_worst(int nheap) {
	int n = 10000;
	//int n = 1;
	int tl = 1000;
	int worst = 0;
	long sum = 0;

	#define NBIN 1000
	static int bin[NBIN];

	int seed = getpid();
	for(int t = 0; t < n; t++) {
		struct core *c[NCORE1] = {c_new(0, GRP1, seed)};
		seed = rand_r(&seed);
		struct sched_state *ss = ss_new(tl, nheap, c, NCORE1);
		struct group *gs[GRP1];
		gs[0] = grp_new(ss->mh, 0, 10);
		struct heap *h = mh_choose_heap(ss->mh, c[0]);

		struct process *p = grp_new_process(ss->mh, 1, gs[0]);
		ss_enqueue(ss, c[0], p);

		for (int i = 0; ; i++) {
			if (ss_schedule(ss, c[0])) {
				sum += i;
				bin[i]++;
				if(i > worst)
					worst = i;
				break;
			}
		}
		cleanup(ss->mh);
	}
	int median;
	int t = 0;
	for (int i = 0; i < NBIN; i++) {
		//printf("%d: %d\n", i, bin[i]);
		t += bin[i];
		if(t >= n / 2) {
			median = i;
			break;
		}
	}
	printf("== test_worst: avg %ld med %d worst %d\n", sum/n, median, worst);
}


void main(int argc, char *argv[]) {
	//debug = true;
	test_grp_sleep_wakeup();
	test_load();
	test_preempt_t();
        test_preempt();
	//exit(1);
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

