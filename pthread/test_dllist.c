#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sched.h>
#include <assert.h>
#include <stdbool.h>

#include "util.h"
#include "core.h"
#include "dllist.h"

#define NCORES 10

int num_cores;
bool do_affinity = false;
bool do_latency = false;
struct core **cores;
int time_to_run = 2;

dllist_t list __calign__;

void test_dllist() {
	bool ok;
	struct core *c = cores[0];
	struct core *c1 = cores[1];

	 dl_init(&list);
	 dl_push(&list, &c->preempt_node);
	 struct dlnode *n = dl_pop(&list);
	 assert(&c->preempt_node == n);
	 struct core *c0 = container_of(n, struct core, preempt_node);
	 assert(c == c0);
	 n = dl_pop(&list);
	 assert(n == NULL);
	 dl_push(&list, &c->preempt_node);
	 dl_push(&list, &c1->preempt_node);
	 bool b = dl_remove(&list, &c->preempt_node);
	 assert(b);
	 n = dl_pop(&list);
	 c0 = container_of(n, struct core, preempt_node);
	 assert(c0 == c1);
	 n = dl_pop(&list);
	 assert(n == NULL);
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

#define N 1000
	// XXX cannot immediately reuse node after removal
	struct dlnode *n = calloc(N, sizeof(struct dlnode));
	for (int i = 0; i < N; i++) {
		// for (int i = 0; now() - start < time_to_run; i++) {
		//printf("%d: %d\n", mycore->cid, i);
		dl_push(&list, &n[i%N]);
		bool b = dl_remove(&list, &n[i%N]);
		assert(b);
	}
}

void test_parallel() {

	dl_init(&list);

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
	test_dllist();
	test_parallel();
}
