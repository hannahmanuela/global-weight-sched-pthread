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
pthread_mutex_t log_lock;

// =========================================================================
// =========================================================================
// HELPER FUNCTIONS
// =========================================================================
// =========================================================================


// ================
// for creation
// ================

struct process *create_process(int id, struct group *group) {
    struct process *p = malloc(sizeof(struct process));
    p->process_id = id;
    p->group = group;
    p->next = NULL;
    return p;
}


struct group *create_group(int id, int weight) {
    struct group *g = malloc(sizeof(struct group));
    g->group_id = id;
    g->weight = weight;
    g->num_threads = 0;
    g->threads_queued = 0;
    g->spec_virt_time = 0;
    g->virt_lag = 0;
    g->last_virt_time = 0;
    g->runqueue_head = NULL;
    g->next = NULL;
    heap_elem_init(&g->heap_elem, g);
    pthread_rwlock_init(&g->group_lock, NULL);
    return g;
}


// ================
// for run_core
// ================


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


// =================
// for group
// =================

int grp_cmp(void *e0, void *e1) {
	struct group *a = (struct group *) e0;
	struct group *b = (struct group *) e1;
	if (a->threads_queued == 0) return 1;
	if (b->threads_queued == 0) return -1;
	// Compare by spec_virt_time; lower is higher priority
	if (a->spec_virt_time < b->spec_virt_time) return -1;
	if (a->spec_virt_time > b->spec_virt_time) return 1;
	// tie-breaker by group_id for determinism
	if (a->group_id < b->group_id) return -1;
	if (a->group_id > b->group_id) return 1;
	return 0;
}

int grp_get_spec_virt_time(struct group *g) {
	pthread_rwlock_rdlock(&g->group_lock);
	int curr_spec_virt_time = g->spec_virt_time;
	if (g->threads_queued == 0) { // deq sets threads_queued before removing the group from the list
		curr_spec_virt_time = INT_MAX;
	}
	pthread_rwlock_unlock(&g->group_lock);
	return curr_spec_virt_time;
}

// set spec_virt_time for group g
// caller should have no locks
void grp_set_spec_virt_time(struct group_list *gl, struct group *g) {
	int initial_virt_time = gl_avg_spec_virt_time(g); 
	pthread_rwlock_wrlock(&g->group_lock);
	if (g->virt_lag > 0) {
		if (g->last_virt_time > initial_virt_time) {
			initial_virt_time = g->last_virt_time; // adding back left over lag only if its still ahead
		}
	} else if (g->virt_lag < 0) {
		initial_virt_time -= g->virt_lag; // negative lag always carries over? maybe limit it?
	}
	g->spec_virt_time = initial_virt_time;
	pthread_rwlock_unlock(&g->group_lock);
}

int grp_get_weight(struct group *g) {
        pthread_rwlock_rdlock(&g->group_lock);
        int p_grp_weight = g->weight;
        pthread_rwlock_unlock(&g->group_lock);
	return p_grp_weight;
}

// update spec time (collapse spec time)
void grp_collapse_spec_virt_time(struct group *g, int time_passed) {
	int p_grp_weight = grp_get_weight(g);
	int time_had_expected = (int) (tick_length / p_grp_weight);

	// printf("%d: collapse: e %d t %d w %d\n", g->group_id, time_had_expected, time_passed, p_grp_weight);
    
	// update spec virt time if time gotten was not what this core expected
	int virt_time_gotten = (int)(time_passed / p_grp_weight);
	if (time_had_expected  != virt_time_gotten) {
		// need to edit the spec time to use time actually got
		int diff = virt_time_gotten - time_had_expected;
		pthread_rwlock_wrlock(&g->group_lock);
		g->spec_virt_time += diff;
		pthread_rwlock_unlock(&g->group_lock);
		gl_fix_group(g);
	}
}


// add p to its group. caller must hold group lock
void grp_add_process(struct process *p, int is_new) {
	struct process *curr_head = p->group->runqueue_head;
	if (!curr_head) {
		p->group->runqueue_head = p;
		p->next = NULL;
	} else {
		p->next = curr_head;
		p->group->runqueue_head = p;
	}
}

// remove p from its group. caller must hold group lock
void grp_del_process(struct process *p) {
	struct process *curr_p = p->group->runqueue_head;
	if (curr_p->process_id == p->process_id) {
		p->group->runqueue_head = curr_p->next;
	} else {
		struct process *prev = curr_p;
		curr_p = curr_p->next;
		while (curr_p) {
			if (curr_p->process_id == p->process_id) {
				prev->next = curr_p->next;
				break;
			}
			prev = curr_p;
			curr_p = curr_p->next;
		}
	}
	p->next = NULL;
}

void grp_dec_nthread(struct group *g) {
	pthread_rwlock_wrlock(&g->group_lock);
	g->num_threads -= 1;
	assert(g->num_threads >= g->threads_queued);
	pthread_rwlock_unlock(&g->group_lock);
}


// =========================================================================
// =========================================================================
// MAIN FUNCTIONS
// =========================================================================
// =========================================================================



// Make p runnable.
// caller should have no locks
void enqueue(struct group_list *gl, struct process *p, int is_new) {
	pthread_rwlock_wrlock(&p->group->group_lock);
	bool was_empty = p->group->threads_queued == 0;
	grp_add_process(p, is_new); 
	if (is_new) {
		p->group->num_threads += 1;
	}
	p->group->threads_queued += 1;
	pthread_rwlock_unlock(&p->group->group_lock);
	if (was_empty) {
		grp_set_spec_virt_time(gl, p->group); 
	}
}


// Assumes caller holds p's group lock
// returns with no locks
void dequeue(struct group_list *gl, struct process *p) {

	grp_del_process(p);

	bool now_empty = p->group->threads_queued == 0;

	pthread_rwlock_unlock(&p->group->group_lock);
    
	if (!now_empty) {
		return;
	}

	// there's a potential race with enq here
	// enq will have set the threads_queued to 1, so re-check before setting
	// unlocking the group before getting gl_avg_spec_virt_time is required because of the lock order
	int curr_avg_spec_virt_time = gl_avg_spec_virt_time(p->group);

	pthread_rwlock_wrlock(&p->group->group_lock);
	if (p->group->threads_queued > 0) { // someone else enq'd while we were deq'ing, don't overwrite the virt time
		pthread_rwlock_unlock(&p->group->group_lock);
		return;
	}
	int spec_virt_time = p->group->spec_virt_time;
	p->group->virt_lag = curr_avg_spec_virt_time - spec_virt_time;
	p->group->last_virt_time = spec_virt_time;
	pthread_rwlock_unlock(&p->group->group_lock);
}

void schedule(struct core_state *core, struct group_list *gl, int time_passed, int should_re_enq) {
	struct process *running_process = core->current_process; 
	struct group *prev_running_group = NULL;

	if (running_process) {
		grp_collapse_spec_virt_time(running_process->group, time_passed);
	}

	if (should_re_enq && running_process) {
		enqueue(gl, running_process, 0);
	}

	core->current_process = NULL;

	struct group *min_group = gl_min_group(gl); // returns with both locks held
	if (min_group == NULL) {
		return;
	}
    
	int time_expecting = (int)tick_length / min_group->weight;
	gl_update_group_svt(min_group, time_expecting);
	min_group->threads_queued -= 1;

	assert(min_group->threads_queued >= 0);

	lh_unlock(min_group->lh);

	// select the next process
	struct process *next_p = min_group->runqueue_head;
	dequeue(gl, next_p); // unlocks the group lock
    

	core->current_process = next_p; // core owns p
	next_p->next = NULL;
}

// NOTE: assume we hold no locks
void yield(struct core_state *core, struct group_list *gl, struct process *p, int time_gotten) {
	grp_dec_nthread(p->group);
	schedule(core, gl, time_gotten, 0);
}


// =========================================================================
// =========================================================================
// RUN FUNCTIONS
// =========================================================================
// =========================================================================

long us_since(struct timeval *start) {
	struct timeval end;
	gettimeofday(&end, NULL);
	long us = (end.tv_sec * 1000000 + end.tv_usec) - (start->tv_sec * 1000000 + start->tv_usec);
	return us;
}

#define SCHED 0
#define ENQ 1
#define YIELD 2

void doop(struct core_state *mycore, int op, long *cycles, long *us, long *n, struct process *p) {
	struct timeval start;
	gettimeofday(&start, NULL);
	long ts = safe_read_tsc();
	switch(op) {
	case SCHED:
		schedule(mycore, gs->glist, tick_length, 1);
		break;
	case ENQ:
	        enqueue(gs->glist, p, 1);
		break;
	case YIELD:
		yield(mycore, gs->glist, p, tick_length/2);
		break;
	}
	long op_cycles = safe_read_tsc() - ts;
	long op_us = us_since(&start);
	*cycles += op_cycles;
	*us += op_us;
	*n += 1;
}

// randomly choose to: "run" for the full tick, "enq" a new process, or "deq" something
void choose(struct core_state *mycore, struct process **pool) {
	int choice = rand() % 3;
	switch(choice) {
	case 0: // Run for full tick
		return;
	case 1: // Make a process runnable
		// pick an exisitng process from the pool?
		struct process *p = *pool;
		if (!p) {
			return;
		}
		*pool = p->next;
		p->next = NULL;

		doop(mycore, ENQ, &mycore->enq_cycles, &mycore->enq_us, &mycore->nenq, p);

		assert_p_in_group(p, p->group);
		usleep(tick_length);
		break;
	case 2: // Yield core
		p = mycore->current_process;
		if (!p) {
			return;
		}
		doop(mycore, YIELD, &mycore->yield_cycles, &mycore->yield_us, &mycore->nyield, p);
		// XXX should 1/2 tick_length?

		assert_p_not_in_group(p, p->group);
		p->next = *pool;
		*pool = p;
		usleep((int)(tick_length / 2));
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
	while (us_since(&start_exp) < TIME_TO_RUN) {
		struct process *prev_running_process = mycore->current_process;

		// gl_print(gs->glist);

		doop(mycore, SCHED, &mycore->sched_cycles, &mycore->sched_us, &mycore->nsched, NULL); 
		if (mycore->current_process) {
			assert_thread_counts_correct(mycore->current_process->group, mycore);
			// assert_threads_queued_correct(mycore->current_process->group);
		}
		struct process *next_running_process = mycore->current_process;
		// usleep(tick_length);   // XXX should this be in choice == 0 branch?

		// choose(mycore, &pool);
	}
}


void main(int argc, char *argv[]) {
    pthread_mutex_init(&log_lock, NULL);

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
        struct group *g = create_group(i, 10);
        gl_register_group(gs->glist, g);
        for (int j = 0; j < num_threads_p_group; j++) {
            struct process *p = create_process(i*num_threads_p_group+j, g);
            enqueue(gs->glist, p, 1);
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



