#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sched.h>
#include <assert.h>
#include <stdbool.h>
#include <unistd.h>

#include "util.h"
#include "core.h"
#include "running.h"

int num_cores = 2;
bool do_affinity = false;
bool do_latency = false;
bool debug = false;
bool use_power2_insert = true;
struct core **cores;
int time_to_run = 2;
struct mheap *mh;


void *run_core(void* core) {
	struct core *mycore = (struct core *) core;
	set_mycore(mycore);

	// pin to an actual core per the selected policy
	int cpu_want = calc_pin_cpu(mycore->cid);

	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(cpu_want, &cpuset);
	if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) != 0)
		error("couldn't set affininity\n");

	double start = now();
	int cid = mycore->cid;
	struct task_struct *p = proc_new(mh, mycore->cid, 0);
	for (int i = 0; now() - start < time_to_run; i++) {
		if (cid != 0) {
			running_set(mh, p, mycore->cid);
			running_clear(mh, p);
		} else {
			running_find_and_clear(mh);
		}
	}
}

void test_parallel() {
	mh = mh_new(num_cores * 2);
	pthread_t *threads = (pthread_t *) malloc(num_cores * sizeof(pthread_t));
	for (int i = 0; i < num_cores; i ++) {
		pthread_create(&threads[i], NULL, run_core, (void*)(cores[i]));
	}
	long clear = 0;
	long find_ok = 0;
	long find_fail = 0;
	long set = 0;
	for (int i = 0; i < num_cores; i++) {
		struct core *c = cores[i];
		pthread_join(threads[c->cid], NULL);
		clear += c->npreempt_clear;
		set += c->npreempt_set;
		find_ok += c->npreempt_find_ok;
		find_fail += c->npreempt_find_fail;
	}
	printf("set %d find %d %d\n", set, find_ok, find_fail);
	printf("tp %0.2fM/s find_fail %d\n", AVG(find_ok+set, time_to_run)/1000000, find_fail);
}

void usage(char *s) {
	fprintf(stderr, "%s: <num_cores>, where num_cores > 1\n", s);
	exit(1);

}

int main(int argc, char *argv[]) {
	int opt;
	
	while ((opt = getopt(argc, argv, "r")) != -1) {
		switch(opt) {
		case 'r':
			break;
		}
	}
	if (argc - optind != 1) {
		usage(argv[0]);
	}

	num_cores = atoi(argv[optind]);
	if (num_cores < 2)
		usage(argv[0]);
	cores = (struct core **) aligned_alloc(CACHE_LINE_SZ, ALIGN_UP(sizeof(struct core *)*num_cores, CACHE_LINE_SZ));
	for (int i = 0; i < num_cores; i++) {
		cores[i] = c_new(i, 1, i);
	}
	test_parallel();
}
