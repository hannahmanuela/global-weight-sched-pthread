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

//#define TRACE
// #define ASSERTS
// #define ASSERTS_SINGLE_WORKER

// #define TIME_TO_RUN 10000000LL
#define TIME_TO_RUN 1000000LL

int num_groups = 100;
int num_cores = 27;
int tick_length = 1000;
int num_threads_p_group = 3;

// lock invariant (to avoid deadlocks): 
//  - always lock global list before group lock, if you're going to lock both 
//      (no locking a group while holding the list lock)

// there are only global datastructures here

struct process {
	int process_id;
	struct group *group;
	struct process *next;
} __attribute__((aligned(64)));

struct group {
	int group_id;
	int weight;

	pthread_rwlock_t group_lock;
	uint64_t nfail;

	int num_threads; // the total number of threads in the system
	int threads_queued; // the number of threads runnable and in the q (ie not running)
	int spec_virt_time; // updated when the group is scheduled, assuming full tick

	int virt_lag; // only has a value when a group is dequeued
	// only has a value when a group is dequeued, and if virt_lag is positive
	// use this to implement delay deq: if g has positie lag when deqed, that lag isn't added on if it is enqed after the lag time has passed
	int last_virt_time; 

	struct process *runqueue_head;
	struct group *next;
	struct heap_elem heap_elem;
} __attribute__((aligned(64)));

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

struct group_list {
	pthread_rwlock_t group_list_lock;
	struct heap *heap;
	long wait_for_wr_group_list_lock_cycles;
	long num_times_wr_group_list_locked;
	atomic_long wait_for_rd_group_list_lock_cycles;
	atomic_long num_times_rd_group_list_locked;

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

long safe_read_tsc() {
    _mm_lfence();
    long ret_val = _rdtsc();
    _mm_lfence();
    return ret_val;
}

// Wrapper functions for pthread_rwlock operations with timing
void timed_pthread_rwlock_wrlock_group_list(pthread_rwlock_t *lock) {
    int start_tsc = safe_read_tsc();
    pthread_rwlock_wrlock(lock);
    int end_tsc = safe_read_tsc();
    gs->glist->wait_for_wr_group_list_lock_cycles += (end_tsc - start_tsc);
    gs->glist->num_times_wr_group_list_locked++;
}

void timed_pthread_rwlock_rdlock_group_list(pthread_rwlock_t *lock) {
    int start_tsc = safe_read_tsc();
    pthread_rwlock_rdlock(lock);
    int end_tsc = safe_read_tsc();
    atomic_fetch_add_explicit(&gs->glist->wait_for_rd_group_list_lock_cycles, (end_tsc - start_tsc), memory_order_relaxed);
    atomic_fetch_add_explicit(&gs->glist->num_times_rd_group_list_locked, 1, memory_order_relaxed);
}

void print_core(struct core_state *c) {
	printf("%ld us(cycles): sched %ld %0.2f(%0.2f) enq %ld %0.2f(%0.2f) yield %ld %0.2f(%0.2f)\n",
	       c - gs->cores,
	       c->nsched, 1.0*c->sched_us/c->nsched, 1.0*c->sched_cycles/c->nsched,
	       c->nenq, 1.0*c->enq_us/c->nenq, 1.0*c->enq_cycles/c->nenq,
	       c->nyield, 1.0*c->yield_us/c->nyield, 1.0*c->yield_cycles/c->nyield);
}

void trace_schedule(long cycles, long us) {
#ifdef TRACE
    printf("sched,%ld,%ld\n", cycles, us);
#endif
}

void trace_yield(long cycles, long us) {
#ifdef TRACE
    printf("yield,%ld,%ld\n", cycles, us);
#endif
}

void trace_enqueue(long cycles, long us) {
#ifdef TRACE
    printf("enq,%ld,%ld\n", cycles, us);
#endif
}

void print_global_state() {
	// pthread_rwlock_rdlock(&gs->glist->group_list_lock);
	timed_pthread_rwlock_rdlock_group_list(&gs->glist->group_list_lock);
	printf("Global heap size: %d \n", gs->glist->heap->heap_size  );
	printf("Heap contents by (group_id: svt, num_threads, num_queued): ");
	for (int i = 0; i < gs->glist->heap->heap_size; i++) {
		struct group *g = (struct group *) heap_lookup(gs->glist->heap, i);
		pthread_rwlock_rdlock(&g->group_lock);
		printf("(%d: %d, %d, %d)%s", g->group_id, g->spec_virt_time, g->num_threads, g->threads_queued, i == gs->glist->heap->heap_size - 1 ? "\n" : ", ");
		pthread_rwlock_unlock(&g->group_lock);
	}
	pthread_rwlock_unlock(&gs->glist->group_list_lock);
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
// for group list
// =================

int grp_get_spec_virt_time(struct group *g);

// -----------------
// heap helpers
// -----------------


static inline int gl_cmp_group(void *e0, void *e1) {
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


void gl_update_group_svt(struct group_list *gl, struct group *g, int diff);

// peek min group without removing; 
// returns with group and list locked
static struct group* gl_peek_min_group(struct group_list *gl) {
    timed_pthread_rwlock_wrlock_group_list(&gl->group_list_lock);
    struct group *g = (struct group *) heap_min(gl->heap);
    if (g && g->threads_queued == 0) {
        g = NULL;
    }
    if (g) {
        pthread_rwlock_wrlock(&g->group_lock);
    }
    return g;
}


// compute avg_spec_virt_time for groups in gl, ignoring group_to_ignore
// caller should have no locks
int gl_avg_spec_virt_time(struct group_list *gl, struct group *group_to_ignore) {
	int total_spec_virt_time = 0;
	int count = 0;
	timed_pthread_rwlock_rdlock_group_list(&gl->group_list_lock);
	for (int i = 0; i < gl->heap->heap_size; i++) {
		struct group *g = (struct group *) heap_lookup(gl->heap, i);
		if (g == group_to_ignore) continue;
		total_spec_virt_time += grp_get_spec_virt_time(g);
		count++;
	}
	pthread_rwlock_unlock(&gl->group_list_lock);
	if (count == 0) return 0;
	return total_spec_virt_time / count;
}


// add group to heap; caller must hold group_list_lock
void gl_add_group(struct group_list *gl, struct group *g) {
	heap_push(gl->heap, &g->heap_elem);
}

// delete group from heap; caller must hold group_list_lock
void gl_del_group(struct group_list *gl, struct group *g) {
	assert(g->heap_elem.heap_index == -1);
	heap_remove_at(gl->heap, &g->heap_elem);
}

// Update group's spec_virt_time and safely reheapify if the group is in the heap.
// caller must hold group_list_lock and group_lock
// keeps both lock held
void gl_update_group_svt(struct group_list *gl, struct group *g, int diff) {
	g->spec_virt_time += diff;
	heap_fix_index(gl->heap, &g->heap_elem);
}


// =================
// for group
// =================

int grp_get_spec_virt_time(struct group *g) {
    pthread_rwlock_rdlock(&g->group_lock);
    int curr_spec_virt_time = g->spec_virt_time;
    if (g->threads_queued == 0) { // deq sets threads_queued before removing the group from the list
        curr_spec_virt_time = INT_MAX;
    }
    pthread_rwlock_unlock(&g->group_lock);
	return curr_spec_virt_time;
}

// set spec_virt_time for new group g
// caller should have no locks
void set_grp_spec_virt_time(struct group_list *gl, struct group *g) {
	int initial_virt_time = gl_avg_spec_virt_time(gl, g); 
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
	return;
}

int grp_get_weight(struct group *g) {
        pthread_rwlock_rdlock(&g->group_lock);
        int p_grp_weight = g->weight;
        pthread_rwlock_unlock(&g->group_lock);
	return p_grp_weight;
}

// update spec time (collapse spec time)
void grp_collapse_spec_virt_time(struct group_list *gl, struct group *g, int time_passed) {
	int p_grp_weight = grp_get_weight(g);
    int time_had_expected = (int) (tick_length / p_grp_weight);
    
    // update spec virt time if time gotten was not what this core expected
    int virt_time_gotten = (int)(time_passed / p_grp_weight);
    if (time_had_expected  != virt_time_gotten) {
        // need to edit the spec time to use time actually got
        int diff = virt_time_gotten - time_had_expected;

        pthread_rwlock_wrlock(&g->group_lock);
        g->spec_virt_time += diff;
        int idx = heap_elem_idx(&g->heap_elem);
        pthread_rwlock_unlock(&g->group_lock);

        // If the group is currently in the heap, fix its position
        if (idx != -1) {
            timed_pthread_rwlock_wrlock_group_list(&gl->group_list_lock);
            heap_fix_index(gl->heap, &g->heap_elem);
            pthread_rwlock_unlock(&gl->group_list_lock);
        }
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
		set_grp_spec_virt_time(gl, p->group); 
	}
    
}

void register_group(struct group_list *gl, struct group *g) {
	timed_pthread_rwlock_wrlock_group_list(&gl->group_list_lock);
	gl_add_group(gl, g);
	pthread_rwlock_unlock(&gl->group_list_lock);
}

void unregister_group(struct group_list *gl, struct group *g) {
	timed_pthread_rwlock_wrlock_group_list(&gl->group_list_lock);
	gl_del_group(gl, g);
	pthread_rwlock_unlock(&gl->group_list_lock);
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
	int curr_avg_spec_virt_time = gl_avg_spec_virt_time(gl, NULL);

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
		grp_collapse_spec_virt_time(gl, running_process->group, time_passed);
	}

	if (should_re_enq && running_process) {
		enqueue(gl, running_process, 0);
	}

	core->current_process = NULL;

	struct group *min_group = gl_peek_min_group(gl); // returns with both locks held
	if (min_group == NULL) {
		pthread_rwlock_unlock(&gl->group_list_lock);
		return;
	}
    

	int time_expecting = (int)tick_length / min_group->weight;
	gl_update_group_svt(gl, min_group, time_expecting);
	min_group->threads_queued -= 1;

	assert(min_group->threads_queued >= 0);

	pthread_rwlock_unlock(&gl->group_list_lock);

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
		yield(mycore, gs->glist, p, tick_length);
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
		//trace_enqueue(enq_cycles, enq_us);
		usleep(tick_length);
		break;
	case 2: // Yield core
		p = mycore->current_process;
		if (!p) {
			return;
		}
		doop(mycore, YIELD, &mycore->yield_cycles, &mycore->yield_us, &mycore->nyield, p);
		// XXX should 1/2 tick_length?
		// print_global_state();

		assert_p_not_in_group(p, p->group);
		// trace_yield(yield_cycles, yield_us);
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

		// print_global_state();

		doop(mycore, SCHED, &mycore->sched_cycles, &mycore->sched_us, &mycore->nsched, NULL); 
		if (mycore->current_process) {
			assert_thread_counts_correct(mycore->current_process->group, mycore);
			// assert_threads_queued_correct(mycore->current_process->group);
		}
		struct process *next_running_process = mycore->current_process;
		// trace_schedule(sched_cycles, sched_us);

		usleep(tick_length);   // XXX should this be in choice == 0 branch?

		// choose(mycore, &pool);
	}
}


void main(int argc, char *argv[]) {
    pthread_mutex_init(&log_lock, NULL);

    // struct sched_param sched_param;
    // sched_param.sched_priority = 99;
    // sched_setscheduler(0, SCHED_FIFO, &sched_param);

    if (argc != 4) {
	    fprintf(stderr, "usage: <num_cores> <tick_length(us)> <num_groups>\n");
	    exit(1);
    }
    num_cores = atoi(argv[1]);
    tick_length = atoi(argv[2]);
    num_groups = atoi(argv[3]);

    gs = malloc(sizeof(struct global_state));
    gs->glist = (struct group_list *) malloc(sizeof(struct group_list));
    gs->glist->heap = NULL;
    gs->glist->heap = heap_new(gl_cmp_group);
    gs->glist->wait_for_wr_group_list_lock_cycles = 0;
    gs->glist->num_times_wr_group_list_locked = 0;
    atomic_init(&gs->glist->wait_for_rd_group_list_lock_cycles, 0);
    atomic_init(&gs->glist->num_times_rd_group_list_locked, 0);
    gs->cores = (struct core_state *) malloc(sizeof(struct core_state)*num_cores);
    pthread_rwlock_init(&gs->glist->group_list_lock, NULL);
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
        for (int j = 0; j < num_threads_p_group; j++) {
            struct process *p = create_process(i*num_threads_p_group+j, g);
            enqueue(gs->glist, p, 1);
        }
        register_group(gs->glist, g);
    }

    // print_global_state();

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
    printf("\nLock timing statistics:\n");
    if (gs->glist->num_times_wr_group_list_locked > 0) {
        printf("Group list write lock: avg %ld cycles (%ld total cycles, %ld operations)\n", 
               gs->glist->wait_for_wr_group_list_lock_cycles / gs->glist->num_times_wr_group_list_locked,
               gs->glist->wait_for_wr_group_list_lock_cycles, gs->glist->num_times_wr_group_list_locked);
    }
    if (gs->glist->num_times_rd_group_list_locked > 0) {
        printf("Group list read lock: avg %ld cycles (%ld total cycles, %ld operations)\n", 
               gs->glist->wait_for_rd_group_list_lock_cycles / gs->glist->num_times_rd_group_list_locked,
               gs->glist->wait_for_rd_group_list_lock_cycles, gs->glist->num_times_rd_group_list_locked);
    }

}





