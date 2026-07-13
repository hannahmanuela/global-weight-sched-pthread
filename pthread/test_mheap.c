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
#include "mpmcv1.h"
#include "mheap.h"

#define NCORES 100

int num_cores;
bool do_affinity = false;
bool do_latency = false;
bool debug = false;
bool use_power2_insert = true;
struct core **cores;
int time_to_run = 2;

struct mheap *mh __calign__;

pthread_barrier_t init_barrier;

void *run_core(void* core) {
	#define N 4

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
		for (long i = 0; i < N * num_cores; i++) {
			struct heap_elem *he = malloc(sizeof(struct heap_elem));
			heap_elem_init(he, safe_read_tsc(), 0);
			mh_insert_elem(mh, he);
		}
	}

	pthread_barrier_wait(&init_barrier);

	double start = now();

	for (int i = 0; now() - start < time_to_run; i++) {
		struct heap_elem *he = mh_deq_min_elem_enq(mh, NULL, false);
		while (he == NULL) {
			he = mh_deq_min_elem_enq(mh, NULL, false);
		}
		mycore->ndeq++;

		he->vruntime = safe_read_tsc();
		mh_insert_elem(mh, he);
		mycore->nenq++;
	}
}

void test_parallel() {
	mh = mh_new(num_cores * 2, is_lt_elem_vt_w);

	pthread_barrier_init(&init_barrier, NULL, num_cores);

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
	printf("tp %d %0.2fM/s\n", num_cores, AVG(nenq+ndeq, time_to_run)/1000000);
}

void test_load() {
	int nheap = 8;
	int ntrial = 100;
	
	extern bool use_power2_insert;
	use_power2_insert = true;

	set_mycore(cores[0]);

	for (int nproc = 2; nproc < 2029; nproc += nproc) {
		int max = 0;
		float a = 0.0;
		for (int t = 0; t < ntrial; t++) {
			mh = mh_new(nheap, is_lt_elem_vt_w);
			for (int i = 0; i < nproc; i++) {
				struct heap_elem *he = malloc(sizeof(struct heap_elem));
				heap_elem_init(he, safe_read_tsc(), 0);
				mh_insert_elem(mh, he);
			}
			int maxl = 0;
			float avg = mh_load(mh, &maxl);
			if (maxl > max)
				max = maxl;
			a = avg;
		}
		printf("n: %d avg %0.2f max %d max load diff %d\n", nproc, a, max, max- (int) a);
	}
}


void test_worst() {
	// int n = 10000;
	int n = 10;
	long sum = 0;
	int worst = 0;
	int nheap = 56 * 2;
	
	#define NBIN 1000
	static int bin[NBIN];

	printf("== test_worst\n");

	set_mycore(cores[0]);

	for(int t = 0; t < n; t++) {
		mh = mh_new(nheap, is_lt_elem_vt_w);
		struct heap_elem *he = malloc(sizeof(struct heap_elem));
		heap_elem_init(he, safe_read_tsc(), 0);
		mh_insert_elem(mh, he);
		for (int i = 0; ; i++) {
			struct heap_elem *he = mh_deq_min_elem(mh, false);
			if(he) {
				sum += i;
				bin[i]++;
				if(i > worst)
					worst = i;
				break;
			}
		}
		free(he);
		mh_free(mh);

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
	printf("--- test_worst: avg %ld med %d worst %d\n", sum/n, median, worst);
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
		cores[i] = c_new(i, 1, getpid() + i);
	}
	test_load();
	test_worst();
	test_parallel();
}
