#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sched.h>
#include <assert.h>
#include <stdbool.h>

#include "util.h"
#include "core.h"
#include "mcounter.h"

int num_cores = 2;
bool do_affinity = false;
bool do_latency = false;
struct core **cores;
int time_to_run = 1;
struct mcntr *mc;

void test_mc() {
	mc_dec(mc, cores[0]);
	bool b = mc_is_zero(mc, cores[0]);
	printf("%d %d\n", b, mc_val(mc));
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

	int cont = 1;
	double start = now();
	for (int i = 0; now() - start < time_to_run; i++) {
		mc_is_zero(mc, mycore);
		mc_inc(mc, mycore);
		mc_dec(mc, mycore);
	}
}

void test_parallel() {
	pthread_t *threads = (pthread_t *) malloc(num_cores * sizeof(pthread_t));
	for (int i = 0; i < num_cores; i ++) {
		pthread_create(&threads[i], NULL, run_core, (void*)(cores[i]));
	}
	long nis_zero = 0;
	long ndec = 0;
	long ninc = 0;
	for (int i = 0; i < num_cores; i++) {
		struct core *c = cores[i];
		pthread_join(threads[c->cid], NULL);
		nis_zero += c->nmc_is_zero;
		ndec += c->nmc_dec;
		ninc += c->nmc_inc;
	}
	long tot = nis_zero + ndec + ninc;
	printf("tp %0.2fM/s\n", AVG(tot, time_to_run)/1000000);
}

void usage(char *s) {
	fprintf(stderr, "%s: <num_cores>, where num_cores > 1\n", s);
	exit(1);

}

int main(int argc, char *argv[]) {
	if (argc != 2) {
		usage(argv[0]);
	}
	num_cores = atoi(argv[1]);
	if (num_cores < 2)
		usage(argv[0]);
	cores = (struct core **) aligned_alloc(CACHE_LINE_SZ, sizeof(struct core *)*num_cores);
	for (int i = 0; i < num_cores; i++) {
		cores[i] = c_new(i, 1, i);
	}
	mc = mc_new();
	assert(mc_is_zero(mc, cores[0]));

	test_mc();
	test_parallel();
}
