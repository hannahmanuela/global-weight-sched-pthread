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
#include "rr1.h"
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

int num_cores;
extern bool debug;
extern bool delay_yield;
extern int scheduler;
extern struct sched_state *ss_global;

void ticks_gettime(t_t *ticks) {
}

void ticks_getidle(t_t *ticks) {
}

void ticks_getwork(t_t *ticks) {
}

static struct task_struct *schedule_retry() {
	struct task_struct *p;
	for (int i = 0; i < 10; i++) {
		if ((p = ss_schedule_rr1(NULL)) != NULL) {
			return p;
		}
	}
	assert(0);
}

static struct sched_state *mk_mheap(int nheap, int ngrp, int nproc, int tl, struct group **gs, int ws[]) {
	tsc_init();
	struct core *cs[NCORE1] = {c_new(0, GRP1, getpid())};
	scheduler = RR1;  // must be set before invoking ss_new()
	set_mycore(cs[0]);
	struct sched_state *ss = ss_new(tl, nheap, cs, NCORE1, is_lt_elem_priority, is_min_elem_high);
	ss_global = ss;
	for (int i = 0; i < ngrp; i++) {
		gs[i] = grp_new(ss->mh, i, ws[i]);
		for (int j = 0; j < nproc; j++) {
			struct task_struct *p = grp_new_process(i * nproc + j, gs[i]);
			ss_enqueue_rr1(p);
		}
	}
	return ss;
}

void test_rr_one_grp() {
	printf("== test_rr_one_grp start\n");

	int tl = 1000;
	struct group *gs[GRP1];
	int ws[GRP1] = {1};
	struct sched_state *ss = mk_mheap(1, GRP1, PROC2, tl, gs, ws);
	struct task_struct *p0;
	struct task_struct *p1;

	p0 = schedule_retry();
	assert(p0);
	p1 = schedule_retry();
	assert(p1);

	assert(p0->he.weight == 1);
	assert(p1->he.weight == 1);
	assert(p1->he.vruntime > p0->he.vruntime);

	printf("== test_rr_one_grp done OK\n");
}

void test_rr_two_grp() {
	printf("== test_rr_two_grp start\n");

	struct group *gs[GRP2];
	int ws[GRP2] = {10, 5};
	int tl = 1000;
	int ngrp = GRP2;

	struct core *c[NCORE1] = {c_new(0, GRP2, 0)};
	struct sched_state *ss = mk_mheap(1, GRP2, PROC2, tl, gs, ws);

	struct task_struct *p0, *p1, *p2, *p3;

	// ss_print(ss, gs, GRP1);
	
	p0 = schedule_retry();
	assert(p0);
	p1 = schedule_retry();
	assert(p1);
	p2 = schedule_retry();
	assert(p2);
	p3 = schedule_retry();
	assert(p3);

	assert(p0->he.weight == 10);
	assert(p1->he.weight == 10);
	assert(p1->he.vruntime > p0->he.vruntime);
	assert(p2->he.weight == 5);
	assert(p3->he.weight == 5);
	assert(p3->he.vruntime > p2->he.vruntime);

	printf("== test_rr_two_grp done OK\n");
}

void main(int argc, char *argv[]) {

	srandom(getpid());

	debug = true;
	test_rr_one_grp();
	test_rr_two_grp();
}

