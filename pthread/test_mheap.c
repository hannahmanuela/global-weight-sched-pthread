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
#include "mheap.h"

#define NCORES 10

int num_cores;
bool do_affinity = false;
bool do_latency = false;
bool debug = false;
bool use_power2_insert = true;
struct core **cores;
int time_to_run = 2;

struct mheap *mh __calign__;

void *run_core(void* core) {
	#define N 64

	struct core *mycore = (struct core *) core;
	set_mycore(mycore);

	// pin to an actual core per the selected policy
	int cpu_want = calc_pin_cpu(mycore->cid);

	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(cpu_want, &cpuset);
	if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) != 0)
		error("couldn't set affininity\n");

	if (mycore->cid == 0) {
		for (long i = 0; i < N; i++) {
			struct task_struct *p = proc_new(mh, i, 0);
			p->he_r.vruntime = safe_read_tsc();
			mh_insert_elem(mh, &p->he_r);
		}
	}

	double start = now();

	for (int i = 0; now() - start < time_to_run; i++) {
		struct task_struct *p = mh_min_proc_enq(mh, NULL, false);
		while (p == NULL) {
			p = mh_min_proc_enq(mh, NULL, false);	
		}
		mycore->ndeq++;

		p->he_r.vruntime = safe_read_tsc();
		mh_insert_elem(p->mh, &p->he_r);
		mycore->nenq++;
	}
}

void test_parallel() {
	mh = mh_new(num_cores * 2);
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

	cores = (struct core **) aligned_alloc(CACHE_LINE_SZ, ALIGN_UP(sizeof(struct core *)*NCORES, CACHE_LINE_SZ));
	for (int i = 0; i < NCORES; i++) {
		cores[i] = c_new(i, 1, i);
	}
	test_parallel();
}
