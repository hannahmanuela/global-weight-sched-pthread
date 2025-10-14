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

int num_groups = 100;
int num_cores = 27;
int tick_length = 1000;

// there are only global datastructures here

struct process {
    int process_id;
    struct group *group;
    struct process *next;
};

struct group {
    int group_id;
    int weight;

    pthread_rwlock_t group_lock;

    int num_threads; // the total number of threads in the system
    int threads_queued; // the number of threads runnable and in the q (ie not running)
    int spec_virt_time; // updated when the group is scheduled, assuming full tick

    int virt_lag; // only has a value when a group is dequeued
    // only has a value when a group is dequeued, and if virt_lag is positive
    // use this to implement delay deq: if g has positie lag when deqed, that lag isn't added on if it is enqed after the lag time has passed
    int last_virt_time; 

    struct process *runqueue_head;
    struct group *next;
};

struct core_state {
	int core_id;
	struct process *current_process;
	int sched_cycles;
	int enq_cycles;
	int yield_cycles;
	int nsched;
	int nenq;
	int nyield;
};

struct group_list {
	pthread_rwlock_t group_list_lock;
	struct group *group_head;
};

struct global_state {
	struct group_list *glist;
	struct core_state *cores;
};

struct global_state* gs;
pthread_mutex_t log_lock;


// ================
// CREATION FUNCTIONS
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
// HELPER FUNCTIONS (for run_core)
// ================

int safe_read_tsc() {
    _mm_lfence();
    int ret_val = _rdtsc();
    _mm_lfence();
    return ret_val;
}


// for printing
int num_cores_running(struct group *group) {
    int num_cores_running = 0;
    for (int i = 0; i < num_cores; i++) {
        if (gs->cores[i].current_process && gs->cores[i].current_process->group == group) {
            num_cores_running++;
        }
    }

    return num_cores_running;
}

// added group_to_ignore so that we are robust against enq/deq interleaving where group is already on the rq when it's not expected to be

void print_core(struct core_state *c) {
	printf("%d cycles: sched %d(%0.2f) enq %d(%0.2f) yield %d (%0.2f)\n",
	       c - gs->cores,
	       c->sched_cycles, 1.0*c->sched_cycles/c->nsched,
	       c->enq_cycles, 1.0*c->enq_cycles/c->nenq,
	       c->yield_cycles, 1.0*c->yield_cycles/c->nyield);
}

void print_global_state() {
    printf("global state:\n");
    struct group *curr_group = gs->glist->group_head;
    while (curr_group) {
        printf("group %d, weight %d, num threads %d, spec virt time %d, num cores running %d\n", curr_group->group_id, curr_group->weight, 
            curr_group->num_threads, curr_group->spec_virt_time, num_cores_running(curr_group));
        curr_group = curr_group->next;
    }

    printf("cores:\n");
    for (int i = 0; i < num_cores; i++) {
        printf("core %d: running process of group %d\n", i, gs->cores[i].current_process ? gs->cores[i].current_process->group->group_id : -1);
    }
}

void write_global_state(FILE *f) {
    fprintf(f, "global state:\n");
    struct group *curr_group = gs->glist->group_head;
    while (curr_group) {
        fprintf(f, "group %d, weight %d, num threads %d, spec virt time %d, num cores running %d\n", curr_group->group_id, curr_group->weight, 
            curr_group->num_threads, curr_group->spec_virt_time, num_cores_running(curr_group));
        curr_group = curr_group->next;
    }

    fprintf(f, "cores:\n");
    for (int i = 0; i < num_cores; i++) {
        fprintf(f, "core %d: running process of group %d \n", i, gs->cores[i].current_process ? gs->cores[i].current_process->group->group_id : -1);
    }
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
// SUPPORT FUNCTIONS for group list
// =================

// find the group with the min spec virt time, and
// return it locked
struct group *gl_find_min_group(struct group_list *gl) {
    struct group *min_group = NULL;
    int min_spec_virt_time = INT_MAX;
    pthread_rwlock_wrlock(&gl->group_list_lock); // not because we are writing, but because we need exclusive access to the group list
    struct group *curr_group = gl->group_head;
    while (curr_group) {
        pthread_rwlock_rdlock(&curr_group->group_lock);
        int curr_spec_virt_time = curr_group->spec_virt_time;
        pthread_rwlock_unlock(&curr_group->group_lock);
        if (curr_spec_virt_time < min_spec_virt_time) {
            min_spec_virt_time = curr_spec_virt_time;
            min_group = curr_group;
        }
        curr_group = curr_group->next;
    }
    if (min_group) {
        pthread_rwlock_wrlock(&min_group->group_lock); // first lock the group then unlock the list lock
        pthread_rwlock_unlock(&gl->group_list_lock);
	return min_group;
    } else {
        pthread_rwlock_unlock(&gl->group_list_lock);
        return NULL;
    }
}

// compute avg_spec_virt_time for groups in gl, ignoring group_to_ignore
int gl_avg_spec_virt_time(struct group_list *gl, struct group *group_to_ignore) {
    int total_spec_virt_time = 0;
    int num_groups = 0;
    pthread_rwlock_rdlock(&gl->group_list_lock);
    struct group *curr_group = gl->group_head;
    while (curr_group) {
        if (curr_group == group_to_ignore) {
            curr_group = curr_group->next;
            continue;
        }
        pthread_rwlock_rdlock(&curr_group->group_lock);
        total_spec_virt_time += curr_group->spec_virt_time;
        pthread_rwlock_unlock(&curr_group->group_lock);

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
        pthread_rwlock_wrlock(&gl->group_list_lock);
        struct group *curr_head = gl->group_head;
        g->next = curr_head; // XXX this is ok because we will check/sync on the next op
        gl->group_head = g;
        pthread_rwlock_unlock(&gl->group_list_lock);
}

void gl_del_group(struct group_list *gl, struct group *g) {
        pthread_rwlock_wrlock(&gl->group_list_lock);
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
        pthread_rwlock_unlock(&gl->group_list_lock);
}

// =================
// SUPPORT FUNCTIONS for group
// =================

// compute spec_virt_time for new group g. assumes caller hold lock for g
int grp_spec_virt_time(struct group_list *gl, struct group *g) {
	int initial_virt_time = gl_avg_spec_virt_time(gs->glist, g);
	if (g->virt_lag > 0) {
		if (g->last_virt_time > initial_virt_time) {
			initial_virt_time = g->last_virt_time; // adding back left over lag only if its still ahead
		}
	} else if (g->virt_lag < 0) {
		initial_virt_time -= g->virt_lag; // negative lag always carries over? maybe limit it?
	}
	return initial_virt_time;
	
}

// update spec time (collapse spec time)
void grp_upd_spec_virt_time(struct group *g, int time_passed) {
        pthread_rwlock_rdlock(&g->group_lock);
        int p_grp_weight = g->weight;
        pthread_rwlock_unlock(&g->group_lock);


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

void grp_dec_nthread(struct group *g) {
        pthread_rwlock_wrlock(&g->group_lock);
        g->num_threads -= 1;
        pthread_rwlock_unlock(&g->group_lock);
}


// ================
// MAIN FUNCTIONS
// ================


// Make p runnable. NOTE: assumes we hold no locks
void enqueue(struct group_list *gl, struct process *p, int is_new) {
	pthread_rwlock_rdlock(&p->group->group_lock);

	if (p->group->threads_queued == 0) {
		int virt_time = grp_spec_virt_time(gs->glist, p->group);

		gl_add_group(gl, p->group);

		// XXX updates p->group while holding on rlock?
		p->group->spec_virt_time = virt_time;
	}

	// XXX updates p->group while holding on rlock?
	grp_add_process(p, is_new);

	pthread_rwlock_unlock(&p->group->group_lock);
}

// Assumes caller holds p's group lock
void dequeue(struct group_list *gl, struct process *p) {
	// XXX should p be removed from its group process list?
	struct group *g = p->group;
	if (g->threads_queued == 1) {
		int curr_avg_spec_virt_time = gl_avg_spec_virt_time(gl, NULL);
		int spec_virt_time = g->spec_virt_time;
		g->virt_lag = curr_avg_spec_virt_time - spec_virt_time;
		g->last_virt_time = spec_virt_time;
	}
	// XXX where should this happen? g->threads_queued -= 1; 
	if (g->threads_queued - 1 == 0) {
		// need to remove group from global group list if now no longer contending
		gl_del_group(gl, g);
	}
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
    if(min_group == NULL)
	    return;

    // select the next process
    int time_expecting = (int)tick_length / min_group->weight;
    min_group->spec_virt_time += time_expecting;

    struct process *next_p = min_group->runqueue_head;
    dequeue(gl, next_p);

    pthread_rwlock_unlock(&min_group->group_lock);  

    core->current_process = next_p; // core owns p
    next_p->next = NULL;
}

// NOTE: assume we hold no locks
void yield(struct core_state *core, struct group_list *gl, struct process *p, int time_gotten) {
	grp_dec_nthread(p->group);
        core->current_process = NULL;
        schedule(core, gl, time_gotten, 0);
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

    struct timespec start_wall, end_wall;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start_wall);

    int cont = 1;
    while (cont) {
	    clock_gettime(CLOCK_MONOTONIC_RAW, &end_wall);
	    long long wall_us = (end_wall.tv_sec - start_wall.tv_sec) * 1000000LL + (end_wall.tv_nsec - start_wall.tv_nsec) / 1000;
	    if (wall_us > 10000000LL) {
		    cont = 0;
	    }

	    struct process *prev_running_process = mycore->current_process;

	    int ts = safe_read_tsc();
	    schedule(mycore, gs->glist, tick_length, 1);
	    mycore->sched_cycles += safe_read_tsc() - ts;
	    mycore->nsched += 1;

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
		    // trace_enqueue(p->process_id, p->group->group_id, core_id);
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
		    // trace_yield(p->process_id, p->group->group_id, core_id);
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
	    fprintf(stderr, "usage: <num_cores> <tick_length> <num_groups>\n");
	    exit(1);
    }
    num_cores = atoi(argv[1]);
    tick_length = atoi(argv[2]);
    num_groups = atoi(argv[3]);

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

    for (int i = 0; i < num_cores; i++) {
        pthread_join(threads[i], NULL);
	print_core(&(gs->cores[i]));
    }

}





