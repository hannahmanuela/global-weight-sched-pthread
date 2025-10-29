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

#include "vt.h"
#include "group.h"
#include "heap.h"
#include "lheap.h"
#include "mheap.h"
#include "global_heap.h"
#include "util.h"

#define TRACE
// #define ASSERTS
// #define ASSERTS_SINGLE_WORKER

// #define TIME_TO_RUN 10000000LL
#define TIME_TO_RUN 1000000LL

int num_groups = 100;
int num_cores = 27;
t_t tick_length = 1000;
int num_threads_p_group = 1;
bool debug = 0;

struct tick {
	t_t tick;
} __attribute__((aligned(64)));
	
struct core_state {
	int core_id;
	struct tick t;
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
	struct core_state *cores;
};

struct global_state* gs;

t_t *new_ticks() {
	return malloc(sizeof(t_t) * num_cores);
}

void ticks_free(t_t *ticks) {
	free(ticks);
}

void ticks_gettime(t_t *ticks) {
	for (int i = 0; i < num_cores; i++)
		ticks[i] = atomic_load(&(gs->cores[i].t.tick));
}

void ticks_sub(t_t *res, t_t *sub) {
	for (int i = 0; i < num_cores; i++) {
		res[i] -= sub[i];
	}
}

void ticks_add(t_t *res, t_t *add) {
	for (int i = 0; i < num_cores; i++) {
		res[i] += add[i];
	}
}

t_t ticks_sum(t_t *ticks) {
	t_t sum = 0;
	for (int i = 0; i < num_cores; i++) {
		sum += ticks[i];
	}
	return sum;
}

void print_core(struct core_state *c) {
	printf("%ld us(cycles): sched %ld %0.2f(%0.2f) enq %ld %0.2f(%0.2f) deq %ld %0.2f(%0.2f) yield %ld %0.2f(%0.2f)",
	       c - gs->cores,
	       c->nsched, 1.0*c->sched_us/c->nsched, 1.0*c->sched_cycles/c->nsched,
	       c->nenq, 1.0*c->enq_us/c->nenq, 1.0*c->enq_cycles/c->nenq,
	       c->ndeq, 1.0*c->deq_us/c->ndeq, 1.0*c->deq_cycles/c->ndeq,
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

void doop(struct core_state *mycore, int op, long *cycles, long *us, long *n, struct process *p) {
	struct timeval start;
	gettimeofday(&start, NULL);
	long ts = safe_read_tsc();
	if(p) p->core_id = mycore - gs->cores; 
	switch(op) {
	case SCHEDULE:
		mycore->current_process = schedule(gs->mh);
		break;
	case YIELD:
		if(p) {
			atomic_fetch_add_explicit(&(mycore->t.tick), tick_length, memory_order_relaxed);
			yield(p, tick_length);
		}
		mycore->current_process = NULL;
		break;
	case ENQ:
	        enqueue(p);
		break;
	case DEQ:
		atomic_fetch_add_explicit(&(mycore->t.tick), tick_length/2, memory_order_relaxed);
		dequeue(p, tick_length/2);
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
		assert_p_in_group(p, p->group);
		break;
	case SLEEP: // Make current process not runnable (e.g., go to sleep)
		p = mycore->current_process;
		if (!p) {
			return;
		}
		doop(mycore, DEQ, &mycore->deq_cycles, &mycore->deq_us, &mycore->ndeq, p);
		assert_p_not_in_group(p, p->group);
		p->next = mycore->pool;
		mycore->pool = p;
		break;
	}
}

void sleepwakeup(struct core_state *mycore) {
	action(mycore, SLEEP);
	action(mycore, WAKEUP);
}

// XXX for 1 core and requires num procs per group to be 1 
void grp_switch_runnable(struct core_state *mycore, struct process *p, int i) {
	static int wakeup = 0;
	if(!p) return;
	if (p->group->group_id == 0) {
		action(mycore, RUN);
	} else {
		if(wakeup == 0) {
			// printf("make group %d unrunnable\n", p->group->group_id);
			action(mycore, SLEEP);
			wakeup = i+2;
		}
	}
	if(i > 0 && wakeup == i) {
		// printf("make group %d runnable\n", p->group->group_id);
		wakeup = 0;
		action(mycore, WAKEUP);
	}
}

void *run_core(void* core_num_ptr) {
	int core_id = (int)core_num_ptr;
	struct core_state *mycore = &(gs->cores[core_id]);


	// pin to an actual core
	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(core_id, &cpuset);
	pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

	struct timeval start_exp;
	gettimeofday(&start_exp, NULL);

	int cont = 1;
	for (int i = 0; us_since(&start_exp) < TIME_TO_RUN; i++) {
		doop(mycore, SCHEDULE, &mycore->sched_cycles, &mycore->sched_us, &mycore->nsched, NULL); 
		if (mycore->current_process) {
			assert_thread_counts_correct(mycore->current_process->group, mycore);
			// assert_threads_queued_correct(mycore->current_process->group);
		}
		// action(mycore, RUN);
		// sleepwakeup(mycore);
		grp_switch_runnable(mycore, mycore->current_process, i);
		// int choice = rand() % 3;
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
    gs->cores = (struct core_state *) malloc(sizeof(struct core_state)*num_cores);
    for (int i = 0; i < num_cores; i++) {
        gs->cores[i].core_id = i;
        gs->cores[i].current_process = NULL;
	gs->cores[i].pool = NULL;
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
    gs->mh = mh_new(grp_cmp, atoi(argv[4]));

    for (int i = 0; i < num_groups; i++) {
	    // struct group *g = grp_new(i, 10);
	    struct group *g = grp_new(i, 10*(i+1));
	    mh_add_group(gs->mh, g);
	    for (int j = 0; j < num_threads_p_group; j++) {
		    struct process *p = grp_new_process(i*num_threads_p_group+j, g);
		    enqueue(p);
	    }
    }

    pthread_t *threads = (pthread_t *) malloc(num_cores * sizeof(pthread_t));

    for (int i = 0; i < num_cores; i ++) {
        pthread_create(&threads[i], NULL, run_core, (void*)i);
    }

    printf("= cores: %d\n", num_cores);
    for (int i = 0; i < num_cores; i++) {
        pthread_join(threads[i], NULL);
	printf("%d: ", i);
	print_core(&gs->cores[i]);
	printf("\n");
    }
    printf("=\n");

    mh_lock_stats(gs->mh);
    mh_runtime_stats(gs->mh);
}



