#define _GNU_SOURCE
#include <limits.h>
#include <stdio.h>
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <sched.h>
#include <assert.h>
#include <stdbool.h>
#include <immintrin.h>
#include <stdint.h> 
#include <sys/resource.h>
#include <stdatomic.h>
#include <strings.h>
#include <float.h>

#include "vt.h"
#include "core.h"
#include "group.h"
#include "heap.h"
#include "mheap.h"
#include "global_heap.h"
#include "rr.h"
#include "util.h"

int time_to_run = 2;  // sec
int num_groups = 4;
int num_cores;
int time_work; // in usec
char *logfile = NULL;
int ratio = 1;
int base_weight = 10;
bool do_ts_op;

// Machine topology (Intel box with HT, 2 sockets x 14 cores x 2 threads):
//   NUMA 0 = even CPUs 0,2,...,54; NUMA 1 = odd CPUs 1,3,...,55.
//   HT siblings: CPU N and CPU N+28 share a physical core.
#define CORES_PER_NUMA 14
#define NUMA_NODES     2
#define HT_PER_CORE    2

int calc_pin_cpu(int cid) {
	int per_numa = CORES_PER_NUMA * HT_PER_CORE;
	int node = cid / per_numa;
	int slot = cid % per_numa;
	return 2 * slot + node;
}

extern bool debug;
extern bool do_affinity;
extern bool do_preempt;
extern bool rr;
extern bool use_localq;
extern bool use_power2_insert;

struct global_state {
	struct global_heap *gh;
	struct group **grps;
	struct core **cores;
};

struct global_state* gs;

void ticks_gettime(t_t *ticks) {
	for (int i = 0; i < num_cores; i++)
		ticks[i] = atomic_load(&(gs->cores[i]->total));
}

void ticks_getidle(t_t *ticks) {
	for (int i = 0; i < num_cores; i++)
		ticks[i] = atomic_load(&(gs->cores[i]->idle));
}

void ticks_getwork(t_t *ticks) {
	for (int i = 0; i < num_cores; i++)
		ticks[i] = atomic_load(&(gs->cores[i]->work));
}

#define SCHEDULE 0
#define YIELD 1
#define ENQ 2
#define DEQ 3

void doop(struct global_heap *gh, struct core *mycore, int op, long *cycles, long *n, struct process *p) {
	long ts = 0;
	if(do_ts_op) ts = safe_read_tsc();
	int c = mycore->cid;
	switch(op) {
	case SCHEDULE:
		long ts;
		if(rr) gh_schedule_rr(gh, mycore);
		else gh_schedule(gh, mycore);
		break;
	case YIELD:
		mycore->total += gh->tick_length;
		if(p) {
			mycore->work += gh->tick_length;
			if (rr) gh_yield_rr(gh, mycore, p, gh->tick_length);
			else gh_yield(gh, mycore, p, gh->tick_length);
		} else {
			mycore->idle += gh->tick_length;
		}
		break;
	case ENQ:
		if (rr) gh_enqueue_rr(gh, mycore, p);
		else gh_enqueue(gh, mycore, p);
		break;
	case DEQ:
		mycore->total += gh->tick_length;
		mycore->work += gh->tick_length/2;
		if(rr) gh_dequeue_rr(gh, mycore, p, gh->tick_length/2);
		else gh_dequeue(gh, mycore, p, gh->tick_length/2);
		break;
	}
	long op_cycles = 0;
	if (do_ts_op) op_cycles = safe_read_tsc() - ts;
	*cycles += op_cycles;
	*n += 1;
}

#define RUN 0
#define WAKEUP 1
#define SLEEP 2

// simulator actions
void action(struct global_heap *gh, struct core *mycore, int choice) {
	switch(choice) {
	case RUN: // Run for full tick
		doop(gh, mycore, YIELD, &mycore->yield_cycles, &mycore->nyield, mycore->process); 
		break;
	case WAKEUP: // Make a process runnable
		// pick an existing process from the pool?
		struct process *p = mycore->pool;
		if (!p) {
			return; 
		}
		mycore->pool = p->next;
		p->next = NULL;
		doop(gh, mycore, ENQ, &mycore->enq_cycles, &mycore->nenq, p);
		break;
	case SLEEP: // Make current process not runnable (e.g., go to sleep)
		p = mycore->process;
		if (!p) {
			return;
		}
		doop(gh, mycore, DEQ, &mycore->deq_cycles, &mycore->ndeq, p);
		p->next = mycore->pool;
		mycore->pool = p;
		break;
	}
}

void rr_groups(int num_groups, int num_threads_p_group) {
	gs->grps = (struct group **) aligned_alloc(CACHE_LINE_SZ, sizeof(struct group *)*num_groups);
	for (int i = 0; i < num_groups; i++) {
		struct mheap *mh = gs->gh->mh;
		if(i == RR_LOW) mh = gs->gh->mh1;
		struct group *g = grp_new(mh, i, 10);
		gs->grps[i] = g;
		for (int j = 0; j < num_threads_p_group; j++) {
			struct process *p = grp_new_process(NULL, i*num_threads_p_group+j, g);
			gh_enqueue_rr(gs->gh, gs->cores[0], p);
		}
	}
}	

void rr_sched_action(struct core *mycore) {
		doop(gs->gh, mycore, SCHEDULE, &mycore->sched_cycles, &mycore->nsched, NULL); 

		if(time_work > 0) usleep(time_work);
		//else usleep(1);  // give another core some time to find low proc

		//action(gs->gh, mycore, SLEEP);

		//doop(gs->gh, mycore, SCHEDULE, &mycore->sched_cycles, &mycore->nsched, NULL); 
		action(gs->gh, mycore, RUN);

		
		// action(gs->gh, mycore, WAKEUP);
}

void global_heap_groups(int num_groups, int num_threads_p_group) {
	gs->grps = (struct group **) aligned_alloc(CACHE_LINE_SZ, sizeof(struct group *)*num_groups);
	w_t w = base_weight;
	for (int i = 0; i < num_groups; i++) {
		struct group *g = grp_new(gs->gh->mh, i, w);
		w  += base_weight * (ratio - 1);
		gs->grps[i] = g;
		for (int j = 0; j < num_threads_p_group; j++) {
			struct process *p = grp_new_process(gs->gh->mh, i*num_threads_p_group+j, g);
			gh_enqueue(gs->gh, gs->cores[0], p);
		}
	}
}

void global_heap_sched_action(struct core *mycore) {
	doop(gs->gh, mycore, SCHEDULE, &mycore->sched_cycles, &mycore->nsched, NULL); 
	if(time_work > 0) 
		usleep(time_work);
	action(gs->gh, mycore, RUN);
	// action(gh, mycore, rand() % 3);
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
	// for (int i = 0; i < 100; i++) {
		for (int i = 0; now() - start < time_to_run; i++) {
		if (rr) rr_sched_action(mycore);
		else global_heap_sched_action(mycore);
	}
}

void usage(char *s) {
	fprintf(stderr, "%s -a -d -g <ngrp> -w <time_to_work (us) -h nheap -r <ratio> -l logfile -t time <num_cores> <num_threads>\n", s);
	exit(1);

}

void main(int argc, char *argv[]) {
	int opt = 0;
	int nheap = 0;
	int tick_length = 1000;

	while ((opt = getopt(argc, argv, "2adpqsg:w:h:r:l:t:")) != -1) {
		switch(opt) {
		case '2':
			use_power2_insert = true;
			break;
		case 'a':
			do_affinity = true;
			break;
		case 'd':
			debug = true;
			break;
		case 'q':
			use_localq = true;
			break;
		case 'p':
			do_preempt = true;
			break;
		case 's':
			num_groups = 1;
			rr = true;
			break;
		case 'g':
			num_groups = atoi(optarg);
			break;
		case 'w':
			time_work = atoi(optarg);
			break;
		case 'h':
			nheap = atoi(optarg);
			break;
		case 'r':
			ratio = atoi(optarg);
			break;
		case 'l':
			logfile = optarg;
			break;
		case 't':
			time_to_run = atoi(optarg);
			break;
		}
	}

	if (argc - optind != 2) {
		usage(argv[0]);
	}
    
	num_cores = atoi(argv[optind]);
	if (nheap == 0)
		nheap = num_cores * 2;
	int num_threads = atoi(argv[optind+1]);
	int num_threads_p_group = num_threads/num_groups;

	gs = malloc(sizeof(struct global_state));
	gs->cores = (struct core **) aligned_alloc(CACHE_LINE_SZ, sizeof(struct core *)*num_cores);
	for (int i = 0; i < num_cores; i++) {
		gs->cores[i] = c_new(i, num_groups, i);
		if (logfile != NULL) c_log_init(gs->cores[i], logfile);
	}
	gs->gh = gh_new(tick_length, nheap, gs->cores, num_cores);
	if (rr) rr_groups(num_groups, num_threads_p_group);
	else global_heap_groups(num_groups, num_threads_p_group);

	// printf("==="); mh_print(gs->gh->mh);

	pthread_t *threads = (pthread_t *) malloc(num_cores * sizeof(pthread_t));
	for (int i = 0; i < num_cores; i ++) {
		pthread_create(&threads[i], NULL, run_core, (void*)(gs->cores[i]));
	}

	printf("= %s num_cores %d num_groups %d nprocs %d (procs/group %d) nheap %d work %d affinity? %d preempt %d localq %d power2_insert %d runtime %ds weight ratio %d\n", rr ? "rr" : "gh", num_cores, num_groups, num_threads, num_threads_p_group, gs->gh->mh->nheap, time_work, do_affinity, do_preempt, use_localq, use_power2_insert, time_to_run, ratio);

	float s_h = 0.0;
	float s_l = FLT_MAX;
	float p_h = 0.0;
	float p_l = FLT_MAX;
	float y_h = 0.0;
	float y_l = FLT_MAX;
	float rins_h = 0.0;
	float rins_l = FLT_MAX;
	float rdel_h = 0.0;
	float rdel_l = FLT_MAX;
	long nretry_ins = 0;
	long nretry_del = 0;
	long nretry_del_lock = 0;
	long y_c = 0;
	long s_c = 0;
	long nsched = 0;
	long nyield = 0;
	long hit = 0;
	long miss = 0;
	long nlocal = 0;
	long nsched_null = 0;
	long max_retry_del = 0;
	long max_retry_del_lock = 0;
	long nnrand = 0;
	long lag_sub_retry = 0;
	long npreempt_retry = 0;
	long npreempt_set = 0;

	for (int i = 0; i < num_cores; i++) {
		struct core *c = gs->cores[i];
		pthread_join(threads[c->cid], NULL);

		c_log_done(c);

		float s = AVG(c->sched_cycles, c->nsched);
		nsched += c->nsched;
		nyield += c->nyield;
		s_h = MAX(s_h, s);
		s_l = MIN(s_l, s);
		s_c += c->sched_cycles;
		// s = AVG(c->min_proc_cycles, c->nsched);
		s = AVG(c->min_proc_cycles, c->nsched+c->nretry_del);
		p_h = MAX(p_h, s);
		p_l = MIN(p_l, s);
		s = AVG(c->yield_cycles, c->nyield);
		y_h = MAX(y_h, s);
		y_c += c->yield_cycles;
		y_l = MIN(y_l, s);
		s = AVG(c->nretry_ins, (c->nenq + c->nyield));
		rins_h = MAX(rins_h, s);
		rins_l = MIN(rins_l, s);
		nretry_ins += c->nretry_ins;
		s = AVG((c->nretry_del+c->nretry_del_lock), c->nsched);
		rdel_h = MAX(rdel_h, s);	
		rdel_l = MIN(rdel_l, s);
		nretry_del += (c->nretry_del + c->nretry_del_lock);
		nretry_del_lock += c->nretry_del_lock;
		lag_sub_retry += c->lag_sub_retry;
		npreempt_retry += c->npreempt_retry;
		npreempt_set += c->npreempt_set;
		nlocal += c->nlocal;

		for (int j = 0; j < num_groups; j++) {
			hit += c->hit[j];
			miss += c->miss[j];
		}

		nsched_null += c->nsched_null;
		if(c->max_retry_del > max_retry_del)
			max_retry_del = c->max_retry_del;
		if(c->max_retry_del_lock > max_retry_del_lock)
			max_retry_del_lock = c->max_retry_del_lock;
		nnrand += c->nrand;
	}
	printf("tp %0.2fM/s\n", AVG(nsched+nyield, time_to_run)/1000000);
	if(p_l > 0) printf(" debug: %0.2f %0.2f)\n", p_l, p_h);
	printf("  sched #%ld(l %ld, g %ld) min %0.2f avg %0.2f max %0.2f\n", nsched, nlocal, nsched-nsched_null-nlocal, s_l, AVG(s_c, nsched), s_h);
	printf("  yield #%ld min %0.2f avg %0.2f max %0.2f\n", nyield, y_l, AVG(y_c, nyield), y_h);
	printf("  retry ins %ld min %0.2f max %0.2f\n", nretry_ins, rins_l, rins_h);
	printf("  retry del %ld (stale %ld) min %0.2f max %0.2f\n", nretry_del, nretry_del_lock, rdel_l, rdel_h);
	printf("    max retry locked %ld stale %ld avg rand %0.2f\n", max_retry_del, max_retry_del_lock, AVG(nnrand, nsched+nretry_del));
	printf("  retry lag sub %ld\n", lag_sub_retry);
	printf("  preempt set %ld retry %ld\n", npreempt_set, npreempt_retry);
	printf("  nsched_null %ld (%0.2f)\n", nsched_null, AVG(nsched_null, nsched));
	if(do_affinity)
		printf("  hit %ld miss %ld hit ratio %0.2f\n", hit, miss, AVG(hit, (hit+miss)));
	for (int i = 0; i < num_cores; i++) {
		struct core *c = gs->cores[i];
		c_print(c, num_groups);
	}
	     
	gh_stats(gs->gh, gs->grps, num_groups);
}



