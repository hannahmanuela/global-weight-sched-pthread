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

#include "heap.h"
#include "lheap.h"
#include "group.h"
#include "group_list.h"
#include "global_heap.h"
#include "util.h"

#define TRACE
// #define ASSERTS
// #define ASSERTS_SINGLE_WORKER

// #define TIME_TO_RUN 10000000LL
#define TIME_TO_RUN 1000000LL

int num_groups = 100;
int num_cores = 27;
int tick_length = 1000;
int num_threads_p_group = 3;

// there are only global datastructures here

struct core_state {
	int core_id;
	struct process *current_process;
	long sched_us;
	long sched_cycles;
	long enq_us;
	long enq_cycles;
	long yield_us;
	long yield_cycles;
	long nsched;
	long nenq;
	long nyield;
} __attribute__((aligned(64)));

struct global_state {
	struct group_list *glist;
	struct core_state *cores;
};

struct global_state* gs;

void print_core(struct core_state *c) {
	printf("%ld us(cycles): sched %ld %0.2f(%0.2f) enq %ld %0.2f(%0.2f) yield %ld %0.2f(%0.2f)\n",
	       c - gs->cores,
	       c->nsched, 1.0*c->sched_us/c->nsched, 1.0*c->sched_cycles/c->nsched,
	       c->nenq, 1.0*c->enq_us/c->nenq, 1.0*c->enq_cycles/c->nenq,
	       c->nyield, 1.0*c->yield_us/c->nyield, 1.0*c->yield_cycles/c->nyield);
}

// =================
// for asserts
// =================

#ifdef ASSERTS

void assert_threads_queued_correct(struct group *g) {
    pthread_rwlock_rdlock(&g->group_lock);
    int num_threads_queued = g->threads_queued;

    int num_p_in_q = 0;
    struct process *curr_p = g->runqueue_head;
    while (curr_p) {
        num_p_in_q++;
        curr_p = curr_p->next;
    }
    assert(num_threads_queued == num_p_in_q);
    pthread_rwlock_unlock(&g->group_lock);
}

#else

void assert_threads_queued_correct(struct group *g) {}

#endif

// the below asserts are only sensical to chek if there is only one worker
#ifdef ASSERTS_SINGLE_WORKER

void assert_p_in_group(struct process *p, struct group *g) {
    assert(num_cores == 1);
    
    struct process *curr_p = g->runqueue_head;
    while (curr_p) {
        if (curr_p->process_id == p->process_id) {
            return;
        }
        curr_p = curr_p->next;
    }
    assert(0);
}

void assert_p_not_in_group(struct process *p, struct group *g) {
    assert(num_cores == 1);
    
    struct process *curr_p = g->runqueue_head;
    while (curr_p) {
        if (curr_p->process_id == p->process_id) {
            assert(0);
        }
        curr_p = curr_p->next;
    }
   return;
}

void assert_thread_counts_correct(struct group *g, struct core_state *core) {
    assert(num_cores == 1);

    int num_threads_queued = 0;
    struct process *curr_p = g->runqueue_head;
    while (curr_p) {
        num_threads_queued++;
        curr_p = curr_p->next;
    }

    assert(num_threads_queued == g->threads_queued);

    if (core->current_process && core->current_process->group == g) {
        assert(g->num_threads == g->threads_queued + 1);
    } else {
        assert(g->num_threads == g->threads_queued);
    }
}


#else

void assert_p_in_group(struct process *p, struct group *g) {}
void assert_p_not_in_group(struct process *p, struct group *g) {}
void assert_thread_counts_correct(struct group *g, struct core_state *core) {}

#endif


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

void doop(struct core_state *mycore, int op, int len, long *cycles, long *us, long *n, struct process *p) {
	struct timeval start;
	gettimeofday(&start, NULL);
	long ts = safe_read_tsc();
	switch(op) {
	case SCHEDULE:
		mycore->current_process = schedule(gs->glist, tick_length);
		break;
	case YIELD:
		yield(p, len, len == tick_length, tick_length);
		mycore->current_process = NULL;
		break;
	case ENQ:
	        enqueue(p, 1);
		break;
	case DEQ:
		dequeue(p, tick_length/2, tick_length);
		mycore->current_process = NULL;
		break;
	}
	long op_cycles = safe_read_tsc() - ts;
	long op_us = us_since(&start);
	*cycles += op_cycles;
	*us += op_us;
	*n += 1;
}

// randomly choose to: "run" for the full tick, "enq" a new process, or "yield" early
int choose(struct core_state *mycore, struct process **pool) {
	int choice = rand() % 3;
	switch(choice) {
	case 0: // Run for full tick
		return tick_length;
	case 1: // Make a process runnable
		// pick an exisitng process from the pool?
		struct process *p = *pool;
		if (!p) {
			return tick_length;
		}
		*pool = p->next;
		p->next = NULL;

		doop(mycore, ENQ, tick_length, &mycore->enq_cycles, &mycore->enq_us, &mycore->nenq, p);
		assert_p_in_group(p, p->group);
		return tick_length;
	case 2: // Make current process not runnable (e.g., go to sleep)
		p = mycore->current_process;
		if (!p) {
			return tick_length;
		}
		assert_p_not_in_group(p, p->group);
		p->next = *pool;
		*pool = p;
		return tick_length/2;
	}
}

void *run_core(void* core_num_ptr) {
	int core_id = (int)core_num_ptr;
	struct core_state *mycore = &(gs->cores[core_id]);

	struct process *pool = NULL;

	// pin to an actual core
	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(core_id, &cpuset);
	pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

	struct timeval start_exp;
	gettimeofday(&start_exp, NULL);

	int cont = 1;
	int len = tick_length;
	while (us_since(&start_exp) < TIME_TO_RUN) {
		// gl_print(gs->glist);
		doop(mycore, YIELD, len, &mycore->yield_cycles, &mycore->yield_us, &mycore->nyield, mycore->current_process); 
		doop(mycore, SCHEDULE, len, &mycore->sched_cycles, &mycore->sched_us, &mycore->nsched, mycore->current_process); 
		if (mycore->current_process) {
			assert_thread_counts_correct(mycore->current_process->group, mycore);
			// assert_threads_queued_correct(mycore->current_process->group);
		}
		// len = choose(mycore, &pool);
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
    tick_length = atoi(argv[2]);
    num_groups = atoi(argv[3]);

    gs = malloc(sizeof(struct global_state));
    gs->glist = gl_new(atoi(argv[4]));
    gs->cores = (struct core_state *) malloc(sizeof(struct core_state)*num_cores);
    for (int i = 0; i < num_cores; i++) {
        gs->cores[i].core_id = i;
        gs->cores[i].current_process = NULL;
        gs->cores[i].sched_us = 0;
        gs->cores[i].enq_us = 0;
        gs->cores[i].yield_us = 0;
        gs->cores[i].sched_cycles = 0;
        gs->cores[i].enq_cycles = 0;
        gs->cores[i].yield_cycles = 0;
        gs->cores[i].nsched = 0;
        gs->cores[i].nenq = 0;
        gs->cores[i].nyield = 0;
    }

    for (int i = 0; i < num_groups; i++) {
        struct group *g = grp_new(i, 10);
        gl_register_group(gs->glist, g);
        for (int j = 0; j < num_threads_p_group; j++) {
            struct process *p = grp_new_process(i*num_threads_p_group+j, g);
            enqueue(p, 1);
        }
    }

    pthread_t *threads = (pthread_t *) malloc(num_cores * sizeof(pthread_t));

    for (int i = 0; i < num_cores; i ++) {
        pthread_create(&threads[i], NULL, run_core, (void*)i);
        // usleep(200);
    }

    for (int i = 0; i < num_cores; i++) {
        pthread_join(threads[i], NULL);
	print_core(&gs->cores[i]);
    }
    // TODO: um I don't unregister the groups for now

    //Print lock timing statistics
    gl_stats(gs->glist);
}



