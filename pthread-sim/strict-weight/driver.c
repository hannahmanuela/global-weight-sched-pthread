#define _GNU_SOURCE
#include <limits.h>
#include <stdio.h>
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#include <sched.h>
#include <assert.h>
#include <stdbool.h>
#include <immintrin.h>
#include <stdint.h> 
#include <sys/resource.h>
#include <stdatomic.h>
#include <strings.h>

#include "vt.h"
#include "ticks.h"
#include "group.h"
#include "heap.h"
#include "lheap.h"
#include "mheap.h"
#include "global_heap.h"
#include "util.h"

#define TRACE

//#define TIME_TO_RUN 10000000LL
#define TIME_TO_RUN 1000000LL

int num_groups = 10;
int num_cores = 8;
int num_threads_p_group = 10;

extern bool debug;

struct core_state {
	int core_id;
	struct tick work;
	struct tick idle;
	struct tick total;
	struct process *current_process;
	struct process *pool;
	long sched_us;
	long sched_cycles;
	long enq_us;
	long enq_cycles;
	long deq_us;
	long deq_cycles;
	long yield_us;
	long yield_cycles;
	long nsched;
	long nenq;
	long ndeq;
	long nyield;
} __attribute__((aligned(64)));

struct global_state {
	struct mheap *mh;
	struct group **grps;
	struct core_state *cores;
};

struct global_state* gs;

void ticks_gettime(t_t *ticks) {
	for (int i = 0; i < num_cores; i++)
		ticks[i] = atomic_load(&(gs->cores[i].total.tick));
}

void ticks_getidle(t_t *ticks) {
	for (int i = 0; i < num_cores; i++)
		ticks[i] = atomic_load(&(gs->cores[i].idle.tick));
}

void ticks_getwork(t_t *ticks) {
	for (int i = 0; i < num_cores; i++)
		ticks[i] = atomic_load(&(gs->cores[i].work.tick));
}

void print_core(struct core_state *c) {
	printf("%d: us(cycles): sched %ld %0.2f(%0.2f) enq %ld %0.2f(%0.2f) deq %ld %0.2f(%0.2f) yield %ld %0.2f(%0.2f)",
	       c - gs->cores,
	       c->nsched, 1.0*c->sched_us/c->nsched, 1.0*c->sched_cycles/c->nsched,
	       c->nenq, 1.0*c->enq_us/c->nenq, 1.0*c->enq_cycles/c->nenq,
	       c->ndeq, 1.0*c->deq_us/c->ndeq, 1.0*c->deq_cycles/c->ndeq,
	       c->nyield, 1.0*c->yield_us/c->nyield, 1.0*c->yield_cycles/c->nyield);
}

long us_since(struct timeval *start) {
	struct timeval end;
	gettimeofday(&end, NULL);
	long us = (end.tv_sec * 1000000 + end.tv_usec) - (start->tv_sec * 1000000 + start->tv_usec);
	return us;
}

#define SCHEDULE 0
#define YIELD 1
#define ENQ 2
#define DEQ 3

void doop(struct core_state *mycore, int op, long *cycles, long *us, long *n, struct process *p) {
	struct timeval start;
	gettimeofday(&start, NULL);
	long ts = safe_read_tsc();
	switch(op) {
	case SCHEDULE:
		mycore->current_process = schedule(mycore-gs->cores, gs->mh);
		break;
	case YIELD:
		atomic_fetch_add(&(mycore->total.tick), gs->mh->tick_length);
		if(p) {
			atomic_fetch_add(&(mycore->work.tick), gs->mh->tick_length);
			yield(p, gs->mh->tick_length);
		} else {
			atomic_fetch_add(&(mycore->idle.tick), gs->mh->tick_length);
		}
		mycore->current_process = NULL;
		break;
	case ENQ:
	        enqueue(p);
		break;
	case DEQ:
		atomic_fetch_add(&(mycore->total.tick), gs->mh->tick_length);
		atomic_fetch_add(&(mycore->work.tick), gs->mh->tick_length/2);
		dequeue(p, gs->mh->tick_length/2);
		mycore->current_process = NULL;
		break;
	}
	long op_cycles = safe_read_tsc() - ts;
	long op_us = us_since(&start);
	*cycles += op_cycles;
	*us += op_us;
	*n += 1;
}

#define RUN 0
#define WAKEUP 1
#define SLEEP 2

// simulator actions
void action(struct core_state *mycore, int choice) {
	switch(choice) {
	case RUN: // Run for full tick
		doop(mycore, YIELD, &mycore->yield_cycles, &mycore->yield_us, &mycore->nyield, mycore->current_process); 
		break;
	case WAKEUP: // Make a process runnable
		// pick an existing process from the pool?
		struct process *p = mycore->pool;
		if (!p) {
			return; 
		}
		mycore->pool = p->next;
		p->next = NULL;
		doop(mycore, ENQ, &mycore->enq_cycles, &mycore->enq_us, &mycore->nenq, p);
		break;
	case SLEEP: // Make current process not runnable (e.g., go to sleep)
		p = mycore->current_process;
		if (!p) {
			return;
		}
		doop(mycore, DEQ, &mycore->deq_cycles, &mycore->deq_us, &mycore->ndeq, p);
		p->next = mycore->pool;
		mycore->pool = p;
		break;
	}
}

void sleepwakeup(struct core_state *mycore) {
	action(mycore, SLEEP);
	action(mycore, WAKEUP);
}

void *run_core(void* core_num_ptr) {
	int core_id = (int)core_num_ptr;
	struct core_state *mycore = &(gs->cores[core_id]);


	// pin to an actual core
	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(core_id, &cpuset);
	if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) < 0)
		error("couldn't set affininity\n");

	struct timeval start_exp;
	gettimeofday(&start_exp, NULL);

	int cont = 1;
	for (int i = 0; us_since(&start_exp) < TIME_TO_RUN; i++) {
		doop(mycore, SCHEDULE, &mycore->sched_cycles, &mycore->sched_us, &mycore->nsched, NULL); 
		action(mycore, RUN);
		// sleepwakeup(mycore);
		// action(mycore, rand() % 3);
	}
}


void main(int argc, char *argv[]) {

    // struct sched_param sched_param;
    // sched_param.sched_priority = 99;
    // sched_setscheduler(0, SCHED_FIFO, &sched_param);

    if (argc != 5) {
	    fprintf(stderr, "usage: <num_cores> <tick_length(us)> <num_groups> <num_heaps>\n");
	    exit(1);
    }
    num_cores = atoi(argv[1]);
    int tick_length = atoi(argv[2]);
    num_groups = atoi(argv[3]);

    gs = malloc(sizeof(struct global_state));
    gs->cores = (struct core_state *) malloc(sizeof(struct core_state)*num_cores);
    for (int i = 0; i < num_cores; i++) {
	    bzero(&(gs->cores[i]), sizeof(struct core_state));
    }
    int seed = 1;
    gs->mh = mh_new(proc_cmp, atoi(argv[4]), seed, tick_length);

    gs->grps = (struct group **) malloc(sizeof(struct group *)*num_groups);
    for (int i = 0; i < num_groups; i++) {
	    struct group *g = grp_new(gs->mh, i, 10);
	    // struct group *g = grp_new(gs->mh, i, 10*(i+1));
	    gs->grps[i] = g;
	    for (int j = 0; j < num_threads_p_group; j++) {
		    struct process *p = grp_new_process(gs->mh, i*num_threads_p_group+j, g);
		    enqueue(p);
	    }
    }

    // mh_print(gs->mh);

    pthread_t *threads = (pthread_t *) malloc(num_cores * sizeof(pthread_t));
    for (int i = 0; i < num_cores; i ++) {
        pthread_create(&threads[i], NULL, run_core, (void*)i);
    }

    printf("= num_cores %d num_groups %d nthreads %d nheap %d\n", num_cores, num_groups, num_groups * num_threads_p_group, gs->mh->nheap);
    printf("= cores: %d\n", num_cores);
    for (int i = 0; i < num_cores; i++) {
        pthread_join(threads[i], NULL);
	print_core(&gs->cores[i]);
	printf("\n");
    }
    printf("=\n");

    mh_lock_stats(gs->mh);
    stats(gs->grps, num_groups);
}



