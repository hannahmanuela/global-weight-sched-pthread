#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sched.h>
#include <assert.h>
#include <stdbool.h>

#include "util.h"
#include "core.h"
#include "mpmcv1.h"

#define NCORES 10

int num_cores;
bool do_affinity = false;
bool do_latency = false;
struct core **cores;
int time_to_run = 2;

queue_t q __calign__;

void test_queue() {
	bool ok;
	void *v;

	queue_init(&q);
	queue_push(&q, (void *) 10);
	queue_push(&q, (void *) 11);
	v = queue_pop(&q);
	assert((long) v == 10);
	v = queue_pop(&q);
	assert((long) v == 11);
	v = queue_pop(&q);
	assert(v == NULL);

	for (long i = 0; i < QUEUE_CAPACITY; i++) {
		queue_push(&q, (void *) i);
	}
	ok = queue_push(&q, (void *) -1);
	assert(!ok);

	v = queue_pop(&q);
	assert((long) v == 0);
	ok = queue_push(&q, (void *) -1);
	assert(ok);

	for (long i = 0; i < QUEUE_CAPACITY-1; i++) {
		v = queue_pop(&q);
		assert((long) v == i+1);
	}

	v = queue_pop(&q);
	assert((long) v == -1);
	v = queue_pop(&q);
	assert(v == NULL);
}

void *run_core(void* core) {
	struct core *mycore = (struct core *) core;

	// pin to an actual core per the selected policy
	int cpu_want = calc_pin_cpu(mycore->cid);

	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(cpu_want, &cpuset);
	if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) != 0)
		error("couldn't set affininity\n");

	double start = now();

	for (int i = 0; now() - start < time_to_run; i++) {
	}
}

void test_parallel() {

	queue_init(&q);

	pthread_t *threads = (pthread_t *) malloc(num_cores * sizeof(pthread_t));
	for (int i = 0; i < num_cores; i ++) {
		pthread_create(&threads[i], NULL, run_core, (void*)(cores[i]));
	}
	long nretry = 0;
	long clear = 0;
	long find_ok = 0;
	long find_fail = 0;
	long set = 0;
	for (int i = 0; i < num_cores; i++) {
		struct core *c = cores[i];
		pthread_join(threads[c->cid], NULL);
		nretry += c->npreempt_retry;
		clear += c->npreempt_clear;
		set += c->npreempt_set;
		find_ok += c->npreempt_find_ok;
		find_fail += c->npreempt_find_fail;
	}
	printf("set %d find %d %d\n", set, find_ok, find_fail);
	printf("tp %0.2fM/s find_fail %d retry %d\n", AVG(find_ok+set, time_to_run)/1000000, find_fail, nretry);
}

void usage(char *s) {
	fprintf(stderr, "%s: <num_cores>\n", s);
	exit(1);

}

int main(int argc, char *argv[]) {
	if (argc != 2) {
		usage(argv[0]);
	}
	num_cores = atoi(argv[1]);
	assert(num_cores <= NCORES);

	cores = (struct core **) aligned_alloc(CACHE_LINE_SZ, sizeof(struct core *)*NCORES);
	for (int i = 0; i < NCORES; i++) {
		cores[i] = c_new(i, 1, i);
	}
	test_queue();
	// test_parallel();
}
