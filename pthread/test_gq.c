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

#define NCORES 100

extern int num_cores;
extern struct core **cores;

bool do_affinity = false;
bool do_latency = false;
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
		void *val = queue_pop(&q);
		// pop may return NULL, even if there items in the queue, retry
		// because there are items in the q
		while (val == NULL) {
			val = queue_pop(&q);
		}
		mycore->ndeq++;
		// similiarly push may return !ok, even when there is space
		bool ok = queue_push(&q, val);
		while (!ok) {
			ok = queue_push(&q, val);
		}
		mycore->nenq++;
		assert(ok);
	}
}

void test_parallel() {

	#define N 64

	queue_init(&q);
	for (long i = 0; i < N; i++) {
		queue_push(&q, (void *) (i+1));
	}

	pthread_t *threads = (pthread_t *) malloc(num_cores * sizeof(pthread_t));
	for (int i = 0; i < num_cores; i ++) {
		pthread_create(&threads[i], NULL, run_core, (void*)(cores[i]));
	}
	long nenq = 0;
	long ndeq = 0;
	for (int i = 0; i < num_cores; i++) {
		struct core *c = cores[i];
		pthread_join(threads[c->cid], NULL);
		nenq += c->nenq;
		ndeq += c->ndeq;
	}
	printf("tp %0.2fM/s\n", AVG(nenq+ndeq, time_to_run)/1000000);
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
	cores_init(NULL);
	test_queue();
	test_parallel();
}
