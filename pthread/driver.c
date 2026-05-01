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
#include <string.h>
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
#include "gwfs.h"
#include "pcrq.h"
#include "util.h"
#include "scheduler.h"

int time_to_run = 2;  // sec
int num_cores;
int time_work; // in usec
char *logfile = NULL;
int base_weight = 10;
bool do_ts_op;
int benchmark = 0;

extern int num_groups;
extern bool debug;
extern bool do_affinity;
extern bool do_preempt;
extern bool rr;
extern bool use_power2_insert;
extern int scheduler;
extern int ratio;

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
		gh_schedule(gh, mycore);
		break;
	case YIELD:
		mycore->total += gh->tick_length;
		if(p) {
			mycore->work += gh->tick_length;
			gh_yield(gh, mycore, p, gh->tick_length);
		} else {
			mycore->idle += gh->tick_length;
		}
		break;
	case ENQ:
		gh_enqueue(gh, mycore, p);
		break;
	case DEQ:
		mycore->total += gh->tick_length;
		mycore->work += gh->tick_length/2;
		gh_dequeue(gh, mycore, p, gh->tick_length/2);
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
	int ns[2];
	gs->grps = (struct group **) aligned_alloc(CACHE_LINE_SZ, sizeof(struct group *)*num_groups);
	assert(num_groups <= 2);
	if(ratio == 1)  {
		ns[0] = num_threads_p_group;
		ns[1] = num_threads_p_group;
	} else {
		ns[0] = 0;
		ns[1] = 2*num_threads_p_group;
	}

	for (int i = 0; i < num_groups; i++) {
		struct mheap *mh = gs->gh->mh;
		if(i == RR_LOW) mh = gs->gh->mh1;
		struct group *g = grp_new(mh, i, 10);
		gs->grps[i] = g;
		for (int j = 0; j < ns[i]; j++) {
			struct process *p = grp_new_process(NULL, i*ns[0]+j, g);
			if(is_pcrq()) gh_enqueue_pcrq(gs->gh, gs->cores[0], p);
			else gh_enqueue_rr(gs->gh, gs->cores[0], p);
		}
	}
}	

void rr_sched_action(struct core *mycore) {
		doop(gs->gh, mycore, SCHEDULE, &mycore->sched_cycles, &mycore->nsched, NULL); 
		if(time_work > 0) usleep(time_work);

		if(benchmark == 1 && (mycore->process != NULL) && mycore->process->pid == 0) {
			// this proc should run after all other runnable procs
			action(gs->gh, mycore, SLEEP);
			action(gs->gh, mycore, WAKEUP);
		} else {
			action(gs->gh, mycore, RUN);
		}
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
	for (int i = 0; now() - start < time_to_run; i++) {
		if (is_rr() || is_pcrq()) rr_sched_action(mycore);
		else global_heap_sched_action(mycore);
	}
}

void usage(char *s) {
	fprintf(stderr, "%s -a -d -g <ngrp> -w <time_to_work (us) -h nheap -r <ratio> -l logfile -t time <sched: gwfs/rr/pcrq> <num_cores> <num_threads>\n", s);
	exit(1);

}

void main(int argc, char *argv[]) {
	int opt = 0;
	int nheap = 0;
	int tick_length = 1000;

	while ((opt = getopt(argc, argv, "2adpqb:g:w:h:r:l:t:")) != -1) {
		switch(opt) {
		case '2':
			use_power2_insert = false;
			break;
		case 'a':
			do_affinity = true;
			break;
		case 'd':
			debug = true;
			break;
		case 'p':
			do_preempt = true;
			break;
		case 'b':
			benchmark = atoi(optarg);
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

	if (argc - optind != 3) {
		usage(argv[0]);
	}
    
	set_scheduler(argv[optind]);
	num_cores = atoi(argv[optind+1]);
	if (nheap == 0) {
		if (is_pcrq()) nheap = num_cores;
		else nheap = num_cores * 2;
	}
	int num_threads = atoi(argv[optind+2]);
	int num_threads_p_group = num_threads/num_groups;

	gs = malloc(sizeof(struct global_state));
	gs->cores = (struct core **) aligned_alloc(CACHE_LINE_SZ, sizeof(struct core *)*num_cores);
	for (int i = 0; i < num_cores; i++) {
		gs->cores[i] = c_new(i, num_groups, i);
		if (logfile != NULL) c_log_init(gs->cores[i], logfile);
	}
	gs->gh = gh_new(tick_length, nheap, gs->cores, num_cores);
	if (is_rr() || is_pcrq()) rr_groups(num_groups, num_threads_p_group);
	else global_heap_groups(num_groups, num_threads_p_group);

	// printf("==="); mh_print(gs->gh->mh);

	pthread_t *threads = (pthread_t *) malloc(num_cores * sizeof(pthread_t));
	for (int i = 0; i < num_cores; i ++) {
		pthread_create(&threads[i], NULL, run_core, (void*)(gs->cores[i]));
	}

	int pg = (is_rr() && (ratio == 0)) ? 0 : num_threads_p_group;
	printf("= %s num_cores %d num_groups %d nprocs %d (procs/group %d) nheap %d work %d affinity? %d preempt %d power2_insert %d benchmark %d runtime %ds weight ratio %d\n", argv[optind], num_cores, num_groups, num_threads, pg, gs->gh->mh->nheap, time_work, do_affinity, do_preempt, use_power2_insert, benchmark, time_to_run, ratio);

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
	float tp = AVG(nsched+nyield, time_to_run)/1000000;
	float tp_p_c = tp/num_cores;
	printf("tp %0.2fM/s per-core %0.2fM  lat sched %0.2fus\n", AVG(nsched+nyield, time_to_run)/1000000, tp_p_c, 1/tp_p_c);
	if(p_l > 0) printf(" debug: %0.2f %0.2f)\n", p_l, p_h);
	printf("  sched #%ld(l %ld, g %ld) min %0.2f avg %0.2f max %0.2f\n", nsched, nlocal, nsched-nlocal, s_l, AVG(s_c, nsched), s_h);
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



