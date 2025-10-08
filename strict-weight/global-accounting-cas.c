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

#define NUM_CORES 56
#define TICK_LENGTH 4000
#define GROUP_BONUS 1000

// there are only global datastructures here

struct process {
    int process_id;
    struct group *group;
    struct process *next;
};

struct group {
    int group_id;
    int weight;

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
};

struct global_state {
    struct group *group_head;
    struct core_state *cores[NUM_CORES];
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
    return g;
}


// ================
// HELPER FUNCTIONS
// ================

int num_cores_running(struct group *group) {
    int num_cores_running = 0;
    for (int i = 0; i < NUM_CORES; i++) {
        if (gs->cores[i]->current_process && gs->cores[i]->current_process->group == group) {
            num_cores_running++;
        }
    }

    return num_cores_running;
}

// added group_to_ignore so that we are robust against enq/deq interleaving where group is already on the rq when it's not expected to be
int avg_spec_virt_time(struct group *group_to_ignore) {
    int total_spec_virt_time = 0;
    int num_groups = 0;
    struct group *curr_group = gs->group_head;
    while (curr_group) {
        if (curr_group == group_to_ignore) {
            curr_group = curr_group->next;
            continue;
        }
        int curr_svt = __atomic_load_n(&curr_group->spec_virt_time, __ATOMIC_ACQUIRE);
        total_spec_virt_time += curr_svt;
        num_groups++;
        curr_group = curr_group->next;
    }
    if (num_groups == 0) {
        return 0;
    }
    return total_spec_virt_time / num_groups;
}

void print_global_state() {
    printf("global state:\n");
    struct group *curr_group = gs->group_head;
    while (curr_group) {
        printf("group %d, weight %d, num threads %d, spec virt time %d, num cores running %d\n", curr_group->group_id, curr_group->weight, 
            curr_group->num_threads, curr_group->spec_virt_time, num_cores_running(curr_group));
        curr_group = curr_group->next;
    }

    printf("cores:\n");
    for (int i = 0; i < NUM_CORES; i++) {
        printf("core %d: running process of group %d\n", i, gs->cores[i]->current_process ? gs->cores[i]->current_process->group->group_id : -1);
    }
}

void write_global_state(FILE *f) {
    fprintf(f, "global state:\n");
    struct group *curr_group = gs->group_head;
    while (curr_group) {
        fprintf(f, "group %d, weight %d, num threads %d, spec virt time %d, num cores running %d\n", curr_group->group_id, curr_group->weight, 
            curr_group->num_threads, curr_group->spec_virt_time, num_cores_running(curr_group));
        curr_group = curr_group->next;
    }

    fprintf(f, "cores:\n");
    for (int i = 0; i < NUM_CORES; i++) {
        fprintf(f, "core %d: running process of group %d \n", i, gs->cores[i]->current_process ? gs->cores[i]->current_process->group->group_id : -1);
    }
}

void trace_schedule(int process_id, int group_id, int prev_group_id, int core_id) {
    pthread_mutex_lock(&log_lock);
    FILE *f = fopen("event_log.txt", "a");
    fprintf(f, "scheduled process %d of group %d on core %d, group changed %d\n", process_id, group_id, core_id, group_id != prev_group_id);
    fclose(f);
    pthread_mutex_unlock(&log_lock);
}

void trace_dequeue(int process_id, int group_id, int core_id) {
    pthread_mutex_lock(&log_lock);
    FILE *f = fopen("event_log.txt", "a");
    fprintf(f, "dequeued process %d of group %d from core %d\n", process_id, group_id, core_id);
    fclose(f);
    pthread_mutex_unlock(&log_lock);
}

void trace_enqueue(int process_id, int group_id, int core_id) {
    pthread_mutex_lock(&log_lock);
    FILE *f = fopen("event_log.txt", "a");
    fprintf(f, "enqueued process %d of group %d on core %d\n", process_id, group_id, core_id);
    fclose(f);
    pthread_mutex_unlock(&log_lock);
}



// ================
// MAIN FUNCTIONS
// ================


void enqueue(struct process *p, int is_new) {

    // printf("enqueueing p %d\n", p->process_id);

    if (__atomic_load_n(&p->group->threads_queued, __ATOMIC_ACQUIRE) == 0) {
        int initial_virt_time = avg_spec_virt_time(p->group);

        int read_last_virt_time = __atomic_load_n(&p->group->last_virt_time, __ATOMIC_ACQUIRE);
        int read_virt_lag = __atomic_load_n(&p->group->virt_lag, __ATOMIC_ACQUIRE);
        if (read_virt_lag > 0) {
            if (read_last_virt_time > initial_virt_time) {
                initial_virt_time = read_last_virt_time; // adding back left over lag only if its still ahead
            }
        } else if (read_virt_lag < 0) {
            initial_virt_time -= read_virt_lag; // negative lag always carries over? maybe limit it?
        }

try_add_group_again:
        struct group *curr_head = gs->group_head;
        p->group->next = curr_head; // this is ok because we will check/sync on the next op
        if (!__atomic_compare_exchange(&gs->group_head, &curr_head, &p->group, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) { // failure memorder is ok b/c we're going to write that mem again in the retry anyway?
            goto try_add_group_again;
        }

        __atomic_store_n(&p->group->spec_virt_time, initial_virt_time, __ATOMIC_RELEASE);
    }

put_in_rq_again:
    struct process *curr_head = p->group->runqueue_head;
    p->next = curr_head; // ok b/c will overwrite if we fail
    if (!__atomic_compare_exchange(&p->group->runqueue_head, &curr_head, &p, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        goto put_in_rq_again;
    }

    if (is_new) {
        __atomic_add_fetch(&p->group->num_threads, 1, __ATOMIC_RELAXED);
    }
    __atomic_add_fetch(&p->group->threads_queued, 1, __ATOMIC_RELAXED);

    // printf("done enqueuing p %d\n", p->process_id);

}


bool dequeue(struct process *p, int was_running, int time_gotten, int core_id);

struct sched_times {
    uint64_t start;
    uint64_t spec_node_rem;
    uint64_t pick_min_group;
    uint64_t get_next_p;
    uint64_t end;
};

struct sched_times schedule(int core_id, int time_passed, int should_re_enq) {
    // printf("scheduling core %d\n", core_id);

    
    struct sched_times ret_val;
    _mm_lfence();
    ret_val.start = _rdtsc();
    _mm_lfence(); 

    struct process *running_process = gs->cores[core_id]->current_process; // cores only read this themselves, so s'all good
    struct group *prev_running_group = NULL;

    // if there was a process running, update spec time (collapse spec time)
    if (running_process) {
        prev_running_group = running_process->group;

        int time_had_expected = (int) (TICK_LENGTH / prev_running_group->weight);
        
        // update spec virt time if time gotten was not what this core expected
        int virt_time_gotten = (int)(time_passed / prev_running_group->weight);
        if (time_had_expected  != virt_time_gotten) {
            // need to edit the spec time to use time actually got
            int diff = time_had_expected - virt_time_gotten;

            __atomic_sub_fetch(&prev_running_group->spec_virt_time, diff, __ATOMIC_RELAXED);
        }

    }

    _mm_lfence();
    ret_val.spec_node_rem = _rdtsc();
    _mm_lfence();


    if (should_re_enq && running_process) {
        enqueue(running_process, 0);
    }

    gs->cores[core_id]->current_process = NULL;

    // pick the group with the min spec virt time
pick_min_group_again:
    struct group *min_group = NULL;
    int min_spec_virt_time = INT_MAX;
    struct group *curr_group = gs->group_head;
    while (curr_group) {
        int threads_queued = __atomic_load_n(&curr_group->threads_queued, __ATOMIC_RELAXED); // ok to be relaxed? the real sync point is writing once we've picked a group
        if (threads_queued > 0) {
            int effective_spec_virt_time = __atomic_load_n(&curr_group->spec_virt_time, __ATOMIC_ACQUIRE);
            if (curr_group == prev_running_group) effective_spec_virt_time -= GROUP_BONUS;
            if (effective_spec_virt_time < min_spec_virt_time) {
                min_spec_virt_time = effective_spec_virt_time;
                min_group = curr_group;
            }
        }
        curr_group = curr_group->next;
    }

    _mm_lfence();
    ret_val.pick_min_group = _rdtsc();
    _mm_lfence();

    if (!min_group) {
        // no group to run
        ret_val.get_next_p = ret_val.pick_min_group;
        goto done;
    }

    // assign the next process
get_next_p_again:
    struct process *next_p = min_group->runqueue_head;
    if (!next_p) {
        goto pick_min_group_again;
    }
    int success = dequeue(next_p, 0, 0, core_id);
    if (!success) {
        goto get_next_p_again;
    }
    

    gs->cores[core_id]->current_process = next_p; // know we right now own the p, so can do with it what we want
    next_p->next = NULL;

    _mm_lfence();
    ret_val.get_next_p = _rdtsc();
    _mm_lfence();

    // update spec time
    int time_expecting = (int)TICK_LENGTH / __atomic_load_n(&next_p->group->weight, __ATOMIC_ACQUIRE);
    __atomic_add_fetch(&next_p->group->spec_virt_time, time_expecting, __ATOMIC_RELAXED);

done:
    _mm_lfence(); 
    ret_val.end = _rdtsc();
    _mm_lfence(); 

    // printf("done scheduling core %d\n", core_id);

    return ret_val;
}

// for now assuming that:
// - if was_running, the process is exiting
// - if !was running, the process is being deqed to be run on this core
bool dequeue(struct process *p, int was_running, int time_gotten, int core_id) {

    // printf("dequeuing core %d\n", core_id);

    if (was_running) {
        __atomic_sub_fetch(&p->group->num_threads, 1, __ATOMIC_ACQUIRE); // the total number of threads in the system has changed
        gs->cores[core_id]->current_process = NULL;
        schedule(core_id, time_gotten, 0);
        return true;
    }

    if (!__atomic_compare_exchange(&p->group->runqueue_head, &p, &p->next, false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
        return false;
    }


    if (__atomic_load_n(&p->group->threads_queued, __ATOMIC_RELEASE) == 1) {
        int curr_avg_spec_virt_time = avg_spec_virt_time(NULL);
        int read_spec_virt_time = __atomic_load_n(&p->group->spec_virt_time, __ATOMIC_ACQUIRE);

        __atomic_store_n(&p->group->virt_lag, curr_avg_spec_virt_time - read_spec_virt_time, __ATOMIC_RELEASE);
        __atomic_store_n(&p->group->last_virt_time, read_spec_virt_time, __ATOMIC_RELEASE);
    }


    int new_threads_queued = __atomic_sub_fetch(&p->group->threads_queued, 1, __ATOMIC_ACQUIRE); // decrease num_queued, only once we succesfully deqed the p
    
    // need to remove group from global group list if now no longer contending
    if (new_threads_queued == 0) {

try_remove_group_again: 
        struct group *curr_group = gs->group_head;
        if (curr_group == p->group) {
            if (!__atomic_compare_exchange(&gs->group_head, &curr_group, &curr_group->next, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
                goto try_remove_group_again;
            }
        } else {
            while (curr_group) {
                if (curr_group->next == p->group) {
                    if (!__atomic_compare_exchange(&curr_group->next, &p->group, &p->group->next, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
                        goto try_remove_group_again;
                    }
                    break;
                }
                curr_group = curr_group->next;
            }
        }

    }
    
    return true;
    // printf("done dequeuing core %d\n", core_id);

}



void run_core(void* core_num_ptr) {

    int core_id = (int)core_num_ptr;

    struct process *pool = NULL;

    // pin to an actual core
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

    while (1) {

        // time how long it takes to schedule, write to file
        struct process *prev_running_process = gs->cores[core_id]->current_process;
        struct timeval start, end;
        gettimeofday(&start, NULL);

        struct sched_times ret_val = schedule(core_id, TICK_LENGTH, 1);
        gettimeofday(&end, NULL);
        long long us_elapsed = (end.tv_sec * 1000000 + end.tv_usec) - (start.tv_sec * 1000000 + start.tv_usec);

        FILE *f = fopen("schedule_time.txt", "a");
        fprintf(f, "total us: %lld --", us_elapsed);
        fprintf(f, "  %lu, %lu, %lu, %lu\n", ret_val.spec_node_rem - ret_val.start, ret_val.pick_min_group - ret_val.spec_node_rem, ret_val.get_next_p - ret_val.pick_min_group, ret_val.end - ret_val.get_next_p);
        fclose(f);

        struct process *next_running_process = gs->cores[core_id]->current_process;
        trace_schedule(next_running_process ? next_running_process->process_id : -1, next_running_process ? next_running_process->group->group_id : -1, prev_running_process ? prev_running_process->group->group_id : -1, core_id);

        usleep(TICK_LENGTH);

        // randomly choose to: "run" for the full tick, "enq" a new process, or "deq" something
        int choice = rand() % 3;
        if (choice == 0) {
            continue; // just run
        } else if (choice == 1) {
            // pick an exisitng process from the pool?
            struct process *p = pool;
            if (!p) {
                continue;
            }
            pool = p->next;
            p->next = NULL;
            enqueue(p, 1);
            trace_enqueue(p->process_id, p->group->group_id, core_id);
            usleep((int)(TICK_LENGTH / 2));
        } else {
            struct process *p = gs->cores[core_id]->current_process;
            if (!p) {
                continue;
            }
            dequeue(p, 1, TICK_LENGTH, core_id);
            trace_dequeue(p->process_id, p->group->group_id, core_id);
            p->next = pool;
            pool = p;
            usleep((int)(TICK_LENGTH / 2));
        }
        
    }


}


void main() {
    pthread_mutex_init(&log_lock, NULL);

    FILE *f = fopen("schedule_time.txt", "w");
    fclose(f);

    f = fopen("event_log.txt", "w");
    fclose(f);

    int num_groups = 10;
    int num_threads_p_group = 20;

    gs = malloc(sizeof(struct global_state));
    gs->group_head = NULL;
    for (int i = 0; i < NUM_CORES; i++) {
        gs->cores[i] = malloc(sizeof(struct core_state));
        gs->cores[i]->core_id = i;
        gs->cores[i]->current_process = NULL;
    }

    for (int i = 0; i < num_groups; i++) {
        struct group *g = create_group(i, 10);
        for (int j = 0; j < num_threads_p_group; j++) {
            struct process *p = create_process(i*num_threads_p_group+j, g);
            enqueue(p, 1);
        }
    }

    // print_global_state();


    pthread_t threads[NUM_CORES]; 

    for (int i = 0; i < NUM_CORES; i ++) {
        pthread_create(&threads[i], NULL, run_core, (void*)i);
        usleep(200);
    }

    for (int i = 0; i < NUM_CORES; i++) {
        pthread_join(threads[i], NULL);
    }

}





