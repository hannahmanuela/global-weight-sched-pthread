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

// #define TRACE
#define ASSERTS
#define ASSERTS_SINGLE_WORKER

// #define TIME_TO_RUN 10000000LL
#define TIME_TO_RUN 10000000LL

int num_groups = 100;
int num_cores = 27;
int tick_length = 1000;
uint8_t num_groups_empty = 0;

// lock invariant (to avoid deadlocks): 
//  - can't lock global list while holding a group lock

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
} __attribute__((aligned(64)));

struct core_state {
	int core_id;
	struct process *current_process;
	int sched_cycles;
	int enq_cycles;
	int yield_cycles;
	int nsched;
	int nenq;
	int nyield;
} __attribute__((aligned(64)));

struct group_list {
	pthread_rwlock_t group_list_lock;
	struct group *group_head;

	uint64_t nfail_min;
	uint64_t nfail_time;
	uint64_t nfail_list;
} __attribute__((aligned(64)));

struct global_state {
	struct group_list *glist;
	struct core_state *cores;
};

struct global_state* gs;
pthread_mutex_t log_lock;


void pthread_rwlock_wrlock_fail(pthread_rwlock_t *l, uint64_t *fail) {
    if (pthread_rwlock_trywrlock(l) != 0) {
            __atomic_add_fetch(fail, 1, __ATOMIC_RELAXED);
            pthread_rwlock_wrlock(l);
    }
}

void pthread_rwlock_rdlock_fail(pthread_rwlock_t *l, uint64_t *fail) {
    if (pthread_rwlock_tryrdlock(l) != 0) {
            __atomic_add_fetch(fail, 1, __ATOMIC_RELAXED);
            pthread_rwlock_rdlock(l);
    }
}

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
    g->spec_virt_time = 0;
    g->virt_lag = 0;
    g->last_virt_time = 0;
    g->runqueue_head = NULL;
    g->next = NULL;
    pthread_rwlock_init(&g->group_lock, NULL);
    return g;
}


// ================
// for run_core
// ================

int safe_read_tsc() {
    _mm_lfence();
    int ret_val = _rdtsc();
    _mm_lfence();
    return ret_val;
}

void print_core(struct core_state *c) {
	printf("%ld cycles: sched %d(%0.2f) enq %d(%0.2f) yield %d (%0.2f)\n",
	       c - gs->cores,
	       c->sched_cycles, 1.0*c->sched_cycles/c->nsched,
	       c->enq_cycles, 1.0*c->enq_cycles/c->nenq,
	       c->yield_cycles, 1.0*c->yield_cycles/c->nyield);
}

void trace_schedule(int process_id, int group_id, int prev_group_id, int core_id) {
#ifdef TRACE
    pthread_mutex_lock(&log_lock);
    FILE *f = fopen("event_log.txt", "a");
    fprintf(f, "scheduled process %d of group %d on core %d, group changed %d\n", process_id, group_id, core_id, group_id != prev_group_id);
    fclose(f);
    pthread_mutex_unlock(&log_lock);
#endif
}

void trace_yield(int process_id, int group_id, int core_id) {
#ifdef TRACE
    pthread_mutex_lock(&log_lock);
    FILE *f = fopen("event_log.txt", "a");
    fprintf(f, "dequeued process %d of group %d from core %d\n", process_id, group_id, core_id);
    fclose(f);
    pthread_mutex_unlock(&log_lock);
#endif
}

void trace_enqueue(int process_id, int group_id, int core_id) {
#ifdef TRACE
    pthread_mutex_lock(&log_lock);
    FILE *f = fopen("event_log.txt", "a");
    fprintf(f, "enqueued process %d of group %d on core %d\n", process_id, group_id, core_id);
    fclose(f);
    pthread_mutex_unlock(&log_lock);
#endif
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

// this assumes that groups never run dry
void assert_num_groups_correct() {
    pthread_rwlock_rdlock(&gs->glist->group_list_lock);
    int num_groups_found = 0;
    struct group *curr_group = gs->glist->group_head;
    while (curr_group) {
        num_groups_found++;
        curr_group = curr_group->next;
    }

    if (num_groups_found + num_groups_empty != num_groups) {
        printf("num_groups_found %d num_groups_empty %d num_groups %d\n", num_groups_found, num_groups_empty, num_groups);
    }
    assert(num_groups_found + num_groups_empty == num_groups);
    pthread_rwlock_unlock(&gs->glist->group_list_lock);
}
#else

void assert_threads_queued_correct(struct group *g) {}
void assert_num_groups_correct() {}

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

void __assert_group_not_in_list(struct group *g) {
    assert(num_cores == 1);
    
    struct group *curr_group = gs->glist->group_head;
    while (curr_group) {
        if (curr_group == g) {
            assert(0);
        }
        curr_group = curr_group->next;
    }
}

void __assert_group_in_list(struct group *g) {
    assert(num_cores == 1);
    struct group *curr_group = gs->glist->group_head;
    while (curr_group) {
        if (curr_group == g) {
            return;
        }
        curr_group = curr_group->next;
    }
    assert(0);
}

void assert_group_list_status_correct(struct group *g) {
    assert(num_cores == 1);

    if (g->threads_queued > 0) {
        __assert_group_in_list(g);
    } else {
        __assert_group_not_in_list(g);
    }
}


#else

void assert_p_in_group(struct process *p, struct group *g) {}
void assert_p_not_in_group(struct process *p, struct group *g) {}
void assert_thread_counts_correct(struct group *g, struct core_state *core) {}
void assert_group_list_status_correct(struct group *g) {}

#endif



// =================
// for group list
// =================

int grp_get_spec_virt_time(struct group *g);

// find the group with the min spec virt time, and
// return it locked. 
struct group *gl_find_min_group(struct group_list *gl) {
	struct group *min_group = NULL;
	int min_spec_virt_time = INT_MAX;

	// acquire lock so that the list doesn't change under out of us
	pthread_rwlock_wrlock_fail(&gl->group_list_lock, &gl->nfail_min); 
	struct group *curr_group = gl->group_head;
	while (curr_group) {
		int curr_spec_virt_time = grp_get_spec_virt_time(curr_group);
		if (curr_spec_virt_time < min_spec_virt_time) {
			min_spec_virt_time = curr_spec_virt_time;
			min_group = curr_group;
		}
		curr_group = curr_group->next;
	}
	if (min_group) {
		// first lock the group then unlock the list lock
		pthread_rwlock_wrlock(&min_group->group_lock);
		pthread_rwlock_unlock(&gl->group_list_lock);
		return min_group;
	} else {
		pthread_rwlock_unlock(&gl->group_list_lock);
		return NULL;
	}
}

// compute avg_spec_virt_time for groups in gl, ignoring group_to_ignore
// caller should have no locks
int gl_avg_spec_virt_time(struct group_list *gl, struct group *group_to_ignore) {
    int total_spec_virt_time = 0;
    int num_groups = 0;
    pthread_rwlock_rdlock_fail(&gl->group_list_lock, &gl->nfail_time);
    struct group *curr_group = gl->group_head;
    while (curr_group) {
        if (curr_group == group_to_ignore) {
            curr_group = curr_group->next;
            continue;
        }
        total_spec_virt_time += grp_get_spec_virt_time(curr_group);
        num_groups++;
        curr_group = curr_group->next;
    }
    pthread_rwlock_unlock(&gl->group_list_lock);
    if (num_groups == 0) {
        return 0;
    }
    return total_spec_virt_time / num_groups;
}


void gl_add_group(struct group_list *gl, struct group *g) {
    pthread_rwlock_wrlock_fail(&gl->group_list_lock, &gl->nfail_list);
    struct group *curr_head = gl->group_head;
    g->next = curr_head; // XXX this is ok because we will check/sync on the next op
    gl->group_head = g;
    num_groups_empty--;
    pthread_rwlock_unlock(&gl->group_list_lock);
}

void gl_del_group(struct group_list *gl, struct group *g) {
    pthread_rwlock_wrlock_fail(&gl->group_list_lock, &gl->nfail_list);
    struct group *curr_group = gl->group_head;
    if (curr_group == g) {
        gl->group_head = curr_group->next;
    } else {
        while (curr_group) {
            if (curr_group->next == g) {
                curr_group->next = g->next;
                break;
            }
            curr_group = curr_group->next;
        }
    }
    num_groups_empty++;
    pthread_rwlock_unlock(&gl->group_list_lock);
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
void grp_upd_spec_virt_time(struct group *g, int time_passed) {
	int p_grp_weight = grp_get_weight(g);
    int time_had_expected = (int) (tick_length / p_grp_weight);
    
    // update spec virt time if time gotten was not what this core expected
    int virt_time_gotten = (int)(time_passed / p_grp_weight);
    if (time_had_expected  != virt_time_gotten) {
        // need to edit the spec time to use time actually got
        int diff = time_had_expected - virt_time_gotten;

        pthread_rwlock_wrlock(&g->group_lock);
        g->spec_virt_time -= diff;
        pthread_rwlock_unlock(&g->group_lock);
    }
}


// add p to its group. caller must hold group lock
void grp_add_process(struct process *p, int is_new) {
	struct process *curr_head = p->group->runqueue_head;
	p->next = curr_head;
	p->group->runqueue_head = p;
	if (is_new) {
		p->group->num_threads += 1;
	}
	p->group->threads_queued += 1;
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

	p->group->threads_queued -= 1;
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
    pthread_rwlock_unlock(&p->group->group_lock);

    if (was_empty) {
        set_grp_spec_virt_time(gl, p->group); 
        gl_add_group(gl, p->group);
    }
    
}

// Assumes caller holds p's group lock
// returns with no locks
void dequeue(struct group_list *gl, struct process *p) {

    grp_del_process(p);

    bool now_empty = p->group->threads_queued == 0;
    pthread_rwlock_unlock(&p->group->group_lock);

    // there's a potential race with enq here
    // removing the group is ok b/c enq will have added a second copy of the group for a bit
    // but setting the virt time is not ok, need to re-check
    if (now_empty) {
        gl_del_group(gl, p->group);
	}

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
	    grp_upd_spec_virt_time(running_process->group, time_passed);
    }

    if (should_re_enq && running_process) {
	    enqueue(gl, running_process, 0);
    }

    core->current_process = NULL;

    struct group *min_group = gl_find_min_group(gl);
    if (min_group == NULL)
	    return;

    // select the next process
    int time_expecting = (int)tick_length / min_group->weight;
    min_group->spec_virt_time += time_expecting;

    struct process *next_p = min_group->runqueue_head;
    dequeue(gl, next_p);

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


void *run_core(void* core_num_ptr) {
    int core_id = (int)core_num_ptr;
    struct core_state *mycore = &(gs->cores[core_id]);

    struct process *pool = NULL;

    // pin to an actual core
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

    struct timespec start_wall, end_wall;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start_wall);

    int cont = 1;
    while (cont) {
	    clock_gettime(CLOCK_MONOTONIC_RAW, &end_wall);
	    long long wall_us = (end_wall.tv_sec - start_wall.tv_sec) * 1000000LL + (end_wall.tv_nsec - start_wall.tv_nsec) / 1000;
	    if (wall_us > TIME_TO_RUN) {
		    cont = 0;
	    }

	    struct process *prev_running_process = mycore->current_process;

	    int ts = safe_read_tsc();
	    schedule(mycore, gs->glist, tick_length, 1);
	    mycore->sched_cycles += safe_read_tsc() - ts;
	    mycore->nsched += 1;

        assert_thread_counts_correct(mycore->current_process->group, mycore);
        if (mycore->current_process) {
            assert_group_list_status_correct(mycore->current_process->group);
            assert_threads_queued_correct(mycore->current_process->group);
        }
        assert_num_groups_correct();
	    struct process *next_running_process = mycore->current_process;
	    trace_schedule(next_running_process ? next_running_process->process_id : -1, next_running_process ? next_running_process->group->group_id : -1, prev_running_process ? prev_running_process->group->group_id : -1, core_id);

	    usleep(tick_length);   // XXX should this be in choice == 0 branch?

	    // randomly choose to: "run" for the full tick, "enq" a new process, or "deq" something
	    int choice = rand() % 3;
	    switch(choice) {
	    case 0: // Run for full tick
		    continue;
	    case 1: // Make a process runnable
		    // pick an exisitng process from the pool?
		    struct process *p = pool;
		    if (!p) {
			    continue;
		    }
		    pool = p->next;
		    p->next = NULL;
		    int ts = safe_read_tsc();
		    enqueue(gs->glist, p, 1);
		    mycore->enq_cycles += safe_read_tsc() - ts;
		    mycore->nenq += 1;

            assert_p_in_group(p, p->group);
		    trace_enqueue(p->process_id, p->group->group_id, core_id);
		    usleep(tick_length);
		    break;
	    case 2: // Yield core
		    p = mycore->current_process;
		    if (!p) {
			    continue;
		    }
		    ts = safe_read_tsc();
		    // XXX should 1/2 tick_length?
		    yield(mycore, gs->glist, p, tick_length);
		    mycore->yield_cycles += safe_read_tsc() - ts;
		    mycore->nyield += 1;

            assert_p_not_in_group(p, p->group);
		    trace_yield(p->process_id, p->group->group_id, core_id);
		    p->next = pool;
		    pool = p;
		    usleep((int)(tick_length / 2));
	    }
        
    }
}


void main(int argc, char *argv[]) {
    pthread_mutex_init(&log_lock, NULL);

    // struct sched_param sched_param;
    // sched_param.sched_priority = 99;
    // sched_setscheduler(0, SCHED_FIFO, &sched_param);

    int num_threads_p_group = 20;
    if (argc != 4) {
	    fprintf(stderr, "usage: <num_cores> <tick_length(us)> <num_groups>\n");
	    exit(1);
    }
    num_cores = atoi(argv[1]);
    tick_length = atoi(argv[2]);
    num_groups = atoi(argv[3]);
    num_groups_empty = num_groups;

    gs = malloc(sizeof(struct global_state));
    gs->glist = (struct group_list *) malloc(sizeof(struct group_list));
    gs->glist->group_head = NULL;
    gs->cores = (struct core_state *) malloc(sizeof(struct core_state)*num_cores);
    pthread_rwlock_init(&gs->glist->group_list_lock, NULL);
    for (int i = 0; i < num_cores; i++) {
        gs->cores[i].core_id = i;
        gs->cores[i].current_process = NULL;
    }

    for (int i = 0; i < num_groups; i++) {
        struct group *g = create_group(i, 10);
        for (int j = 0; j < num_threads_p_group; j++) {
            struct process *p = create_process(i*num_threads_p_group+j, g);
            enqueue(gs->glist, p, 1);
        }
    }

    // print_global_state();

    pthread_t *threads = (pthread_t *) malloc(num_cores * sizeof(pthread_t));

    for (int i = 0; i < num_cores; i ++) {
        pthread_create(&threads[i], NULL, run_core, (void*)i);
        // usleep(200);
    }

    printf("results for %d cores %d us tick, %d groups\n",  num_cores, tick_length, num_groups);
    for (int i = 0; i < num_cores; i++) {
        pthread_join(threads[i], NULL);
	    print_core(&(gs->cores[i]));
    }
    printf("failed glist trylock min %ld time %ld list %ld\n", gs->glist->nfail_min, gs->glist->nfail_time, gs->glist->nfail_list);

}





