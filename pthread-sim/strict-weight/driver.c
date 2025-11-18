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
#include "ticks.h"
#include "core.h"
#include "group.h"
#include "heap.h"
#include "mheap.h"
#include "global_heap.h"
#include "util.h"

#define TRACE

// #define TIME_TO_RUN 20  // sec
#define TIME_TO_RUN 1  // sec

int num_groups = 4;
int num_cores;
int time_work; // in usec

extern bool debug;

struct global_state {
	struct mheap *mh;
	struct group **grps;
	struct core **cores;
};

struct global_state* gs;

void ticks_gettime(t_t *ticks) {
	for (int i = 0; i < num_cores; i++)
		ticks[i] = atomic_load(&(gs->cores[i]->total.tick));
}

void ticks_getidle(t_t *ticks) {
	for (int i = 0; i < num_cores; i++)
		ticks[i] = atomic_load(&(gs->cores[i]->idle.tick));
}

void ticks_getwork(t_t *ticks) {
	for (int i = 0; i < num_cores; i++)
		ticks[i] = atomic_load(&(gs->cores[i]->work.tick));
}

#define SCHEDULE 0
#define YIELD 1
#define ENQ 2
#define DEQ 3

void doop(struct core *mycore, int op, long *cycles, long *n, struct process *p) {
	long ts = safe_read_tsc();
	int c = mycore->cid;
	switch(op) {
	case SCHEDULE:
		long ts;
		mycore->current_process = schedule(mycore, gs->mh);
		break;
	case YIELD:
		atomic_fetch_add(&(mycore->total.tick), gs->mh->tick_length);
		if(p) {
			atomic_fetch_add(&(mycore->work.tick), gs->mh->tick_length);
			yield(mycore, p, gs->mh->tick_length);
		} else {
			atomic_fetch_add(&(mycore->idle.tick), gs->mh->tick_length);
		}
		// mycore->current_process = NULL;
		break;
	case ENQ:
	        enqueue(mycore, p);
		break;
	case DEQ:
		atomic_fetch_add(&(mycore->total.tick), gs->mh->tick_length);
		atomic_fetch_add(&(mycore->work.tick), gs->mh->tick_length/2);
		dequeue(mycore, p, gs->mh->tick_length/2);
		mycore->current_process = NULL;
		break;
	}
	long op_cycles = safe_read_tsc() - ts;
	*cycles += op_cycles;
	*n += 1;
}

#define RUN 0
#define WAKEUP 1
#define SLEEP 2

// simulator actions
void action(struct core *mycore, int choice) {
	switch(choice) {
	case RUN: // Run for full tick
		doop(mycore, YIELD, &mycore->yield_cycles, &mycore->nyield, mycore->current_process); 
		break;
	case WAKEUP: // Make a process runnable
		// pick an existing process from the pool?
		struct process *p = mycore->pool;
		if (!p) {
			return; 
		}
		mycore->pool = p->next;
		p->next = NULL;
		doop(mycore, ENQ, &mycore->enq_cycles, &mycore->nenq, p);
		break;
	case SLEEP: // Make current process not runnable (e.g., go to sleep)
		p = mycore->current_process;
		if (!p) {
			return;
		}
		doop(mycore, DEQ, &mycore->deq_cycles, &mycore->ndeq, p);
		p->next = mycore->pool;
		mycore->pool = p;
		break;
	}
}

void sleepwakeup(struct core *mycore) {
	action(mycore, SLEEP);
	action(mycore, WAKEUP);
}

void *run_core(void* core) {
	struct core *mycore = (struct core *) core;

	// pin to an actual core
	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(mycore->cid, &cpuset);
	if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) < 0)
		error("couldn't set affininity\n");

	int cont = 1;
	double start = now();
	//int fd = perf_config(mycore->cid);
	for (int i = 0; now() - start < TIME_TO_RUN; i++) {
		doop(mycore, SCHEDULE, &mycore->sched_cycles, &mycore->nsched, NULL); 
		if(time_work > 0) 
			usleep(time_work);
		action(mycore, RUN);
		// sleepwakeup(mycore);
		// action(mycore, rand() % 3);
	}
	//printf("perf %ld\n", perf_read_l2(fd));
}


void main(int argc, char *argv[]) {
    if (argc != 5) {
	    fprintf(stderr, "usage: <num_cores> <num_threads> <num_heaps> <time_work (us)>\n");
	    exit(1);
    }
    num_cores = atoi(argv[1]);
    int tick_length = 1000;
    int num_threads = atoi(argv[2]);
    int nheap = atoi(argv[3]);
    int num_threads_p_group = num_threads/num_groups;
    time_work = atoi(argv[4]);

    //debug = true;

    gs = malloc(sizeof(struct global_state));
    gs->cores = (struct core **) aligned_alloc(CACHE_LINE_SZ, sizeof(struct core *)*num_cores);
    for (int i = 0; i < num_cores; i++) {
	    gs->cores[i] = c_new(i);
	    c_log_init(gs->cores[i], "/tmp/vtlog");
    }
    gs->mh = mh_new(proc_cmp, nheap, tick_length);

    gs->grps = (struct group **) aligned_alloc(CACHE_LINE_SZ, sizeof(struct group *)*num_groups);
    for (int i = 0; i < num_groups; i++) {
	    // struct group *g = grp_new(gs->mh, i, 10);
	    struct group *g = grp_new(gs->mh, i, 10*(i+1));
	    gs->grps[i] = g;
	    for (int j = 0; j < num_threads_p_group; j++) {
		    struct process *p = grp_new_process(gs->mh, i*num_threads_p_group+j, g);
		    enqueue(gs->cores[0], p);
	    }
    }

    // printf("==="); mh_print(gs->mh);

    pthread_t *threads = (pthread_t *) malloc(num_cores * sizeof(pthread_t));
    for (int i = 0; i < num_cores; i ++) {
	    pthread_create(&threads[i], NULL, run_core, (void*)(gs->cores[i]));
    }

    printf("= num_cores %d num_groups %d nprocs %d nheap %d work %d\n", num_cores, num_groups, num_threads, gs->mh->nheap, time_work);

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
    long nsched_null = 0;
    long max_retry_del = 0;
    long max_retry_del_lock = 0;
    long nnrand = 0;

    for (int i = 0; i < num_cores; i++) {
	    struct core *c = gs->cores[i];
	    pthread_join(threads[c->cid], NULL);
	    // c_print(); printf("\n");

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
	    hit += c->hit;
	    nsched_null += c->nsched_null;
	    if(c->max_retry_del > max_retry_del)
		    max_retry_del = c->max_retry_del;
	    if(c->max_retry_del_lock > max_retry_del_lock)
		    max_retry_del_lock = c->max_retry_del_lock;
	    nnrand += c->nrand;
    }
    printf("tp %0.2fM/s (debug: %0.2f %0.2f)\n", AVG(nsched+nyield, TIME_TO_RUN)/1000000, p_l, p_h);
    printf("  sched #%ld min %0.2f avg %0.2f max %0.2f\n", nsched, s_l, AVG(s_c, nsched), s_h);
    printf("  yield #%ld min %0.2f avg %0.2f max %0.2f\n", nyield, y_l, AVG(y_c, nyield), y_h);
    printf("  retry ins %ld min %0.2f max %0.2f\n", nretry_ins, rins_l, rins_h);
    printf("  retry del %ld (%ld) min %0.2f max %0.2f\n", nretry_del, nretry_del_lock, rdel_l, rdel_h);
    printf("    max retry locked %d stale %d avg rand %0.2f\n", max_retry_del, max_retry_del_lock, AVG(nnrand, nsched+nretry_del));
    printf("  nsched_null %ld (%0.2f)\n", nsched_null, AVG(nsched_null, nsched));
    printf("  hit %ld\n", hit);
	     
    stats(gs->grps, num_groups);
}



