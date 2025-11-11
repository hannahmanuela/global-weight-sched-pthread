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
#include "lheap.h"
#include "mheap.h"
#include "global_heap.h"
#include "util.h"

#define TRACE

// #define TIME_TO_RUN 20  // sec
#define TIME_TO_RUN 10  // sec

int num_groups = 4;
int num_cores;
int time_work; // in usec

extern bool debug;
extern bool with_tsc;

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

    printf("= num_cores %d num_groups %d nthreads %d nheap %d work %d\n", num_cores, num_groups, num_threads, gs->mh->nheap, time_work);
    printf("= cores: %d\n", num_cores);

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

    float l_i = FLT_MAX;
    float h_i = 0.0;
    float l_r = FLT_MAX;
    float h_r = 0.0;
    float l_cycles = FLT_MAX;
    float h_cycles = 0.0;
    long a_cycles = 0;
    long a_n = 0; 

    for (int i = 0; i < num_cores; i++) {
	    struct core *c = gs->cores[i];
	    pthread_join(threads[c->cid], NULL);
	    // c_print(); printf("\n");
	    printf("max retry %d %d\n", c->max_retry_del, c->max_retry_del_lock);
	    float s = AVG(c->sched_cycles, c->nsched);
	    nsched += c->nsched;
	    nyield += c->nyield;
	    s_h = MAX(s_h, s);
	    s_l = MIN(s_l, s);
	    s_c += c->sched_cycles;
	    s = AVG(c->min_proc_cycles, c->nsched);
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

	    float in = AVG(c->insert_cycles, c->ninsert);
	    float out = AVG(c->remove_cycles, c->nremove);
	    l_i = MIN(l_i, in);
	    h_i = MAX(h_i, in);
	    l_r = MIN(l_r, out);
	    h_r = MAX(h_r, out);
	    float hlc = AVG(c->wait_for_wr_heap_lock_cycles, c->num_times_wr_heap_locked);
	    l_cycles = MIN(l_cycles, hlc);
	    h_cycles = MAX(h_cycles, hlc);
	    a_cycles += c->wait_for_wr_heap_lock_cycles;
	    a_n += c->num_times_wr_heap_locked;
    }
    printf("  sched #%ld l %0.2f a %0.2f h %0.2f min_proc %0.2f %0.2f yield #%ld l %0.2f a %0.2f h %0.2f\n",
	   nsched, s_l, AVG(s_c, nsched), s_h, p_l, p_h, nyield, y_l, AVG(y_c, nyield), y_h);
    printf("  retry ins %ld %0.2f %0.2f retry del %ld (%ld) %0.2f %0.2f\n", nretry_ins, rins_l, rins_h, nretry_del, nretry_del_lock, rdel_l, rdel_h);
    printf("  nsched_null %ld (%0.2f) hit %ld\n", nsched_null, AVG(nsched_null, nsched), hit);

    printf("  cycles: insert l %0.2f h %0.2f remove l %0.2f h %0.2f\n", l_i, h_i, l_r, h_r); 
    printf("  lock cycles l %0.2f a %0.2f h %0.2f\n", l_cycles, AVG(a_cycles, a_n), h_cycles);

    printf("=\n");

    stats(gs->grps, num_groups);
}



