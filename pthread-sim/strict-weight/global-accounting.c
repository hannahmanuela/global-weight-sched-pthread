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

#define NUM_CORES 56
#define TICK_LENGTH 4000
#define GROUP_BONUS 1000

// THIS FILE IS OUTDATED

// there are only global datastructures here

struct process {
    int process_id;
    struct group *group;
    struct process *next;
};

struct spec_time {
    double time_expecting;
    int core_running;
    struct spec_time *next;
};

struct group {
    int group_id;
    int weight;
    int num_threads;
    double spec_virt_time; // updated when the group is scheduled, assuming full tick
    struct spec_time *spec_time_head; // keeps track of who speculated what
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
pthread_mutex_t global_lock;
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
    g->spec_virt_time = -1;
    g->spec_time_head = NULL;
    g->runqueue_head = NULL;
    g->next = NULL;
    return g;
}

struct spec_time *create_spec_time(double time_expecting, int core_running) {
    struct spec_time *s = malloc(sizeof(struct spec_time));
    s->time_expecting = time_expecting;
    s->core_running = core_running;
    s->next = NULL;
    return s;
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

    int num_cores_speculating = 0;
    struct spec_time *curr_spec_time = group->spec_time_head;
    while (curr_spec_time) {
        num_cores_speculating++;
        curr_spec_time = curr_spec_time->next;
    }

    assert(num_cores_running == num_cores_speculating);

    return num_cores_running;
}

int num_cores_speculating(struct group *group) {
    int num_cores_speculating = 0;
    struct spec_time *curr_spec_time = group->spec_time_head;
    while (curr_spec_time) {
        num_cores_speculating++;
        curr_spec_time = curr_spec_time->next;
    }
    return num_cores_speculating;
}

void print_global_state() {
    printf("global state:\n");
    struct group *curr_group = gs->group_head;
    while (curr_group) {
        printf("group %d, weight %d, num threads %d, spec virt time %f, num cores running %d\n", curr_group->group_id, curr_group->weight, 
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
        fprintf(f, "group %d, weight %d, num threads %d, spec virt time %f, num cores running %d\n", curr_group->group_id, curr_group->weight, 
            curr_group->num_threads, curr_group->spec_virt_time, num_cores_running(curr_group));
        curr_group = curr_group->next;
    }

    fprintf(f, "cores:\n");
    for (int i = 0; i < NUM_CORES; i++) {
        fprintf(f, "core %d: running process of group %d \n", i, gs->cores[i]->current_process ? gs->cores[i]->current_process->group->group_id : -1);
    }
}

void trace_schedule(int process_id, int group_id, int group_changed, int core_id) {
    pthread_mutex_lock(&log_lock);
    FILE *f = fopen("event_log.txt", "a");
    fprintf(f, "scheduled process %d of group %d on core %d, group changed %d\n", process_id, group_id, core_id, group_changed);
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

    if (p->group->num_threads == 0) {
        struct group *curr_head = gs->group_head;
        gs->group_head = p->group;
        p->group->next = curr_head;

        assert(p->group->spec_virt_time == -1);
        int min_spec_virt_time = INT_MAX;
        struct group *curr_group = gs->group_head;
        while (curr_group) {
            if (curr_group->spec_virt_time < min_spec_virt_time) {
                min_spec_virt_time = curr_group->spec_virt_time;
            }
            curr_group = curr_group->next;
        }
        p->group->spec_virt_time = min_spec_virt_time;
    }

    struct process *curr_head = p->group->runqueue_head;
    p->group->runqueue_head = p;
    p->next = curr_head;

    if (is_new) {
        p->group->num_threads += 1;
    }

    // printf("done enqueuing p %d\n", p->process_id);

}


void schedule(int core_id, int time_passed, int should_re_enq) {
    // printf("scheduling core %d\n", core_id);

    struct process *running_process = gs->cores[core_id]->current_process;
    struct group *prev_running_group = NULL;

    // if there was a process running, update spec time (collapse spec time)
    if (running_process) {
        prev_running_group = running_process->group;

        struct spec_time *curr_spec_node = prev_running_group->spec_time_head;
        assert(curr_spec_node);
        // remove spec time struct and free memory, remember time had expected
        int time_had_expected = -1;
        if (curr_spec_node->core_running == core_id) {
            time_had_expected = curr_spec_node->time_expecting;
            prev_running_group->spec_time_head = curr_spec_node->next;
            free(curr_spec_node);
        } else {
            struct spec_time *prev = curr_spec_node;
            curr_spec_node = curr_spec_node->next;
            while (curr_spec_node) {
                if (curr_spec_node->core_running == core_id) {
                    time_had_expected = curr_spec_node->time_expecting;
                    prev->next = curr_spec_node->next;
                    free(curr_spec_node);
                    break;
                }
                prev = curr_spec_node;
                curr_spec_node = curr_spec_node->next;
            }
        }
        
        // update spec virt time if time gotten was not what this core expected
        assert(time_had_expected != -1); // we picked it, so must have expected time
        double virt_time_gotten = time_passed / prev_running_group->weight;
        if (time_had_expected  != virt_time_gotten) {
            // need to edit the spec time to use time actually got
            double diff = time_had_expected - virt_time_gotten;
            prev_running_group->spec_virt_time -= diff;
        }

    }


    if (should_re_enq && running_process) {
        enqueue(running_process, 0);
    }

    gs->cores[core_id]->current_process = NULL;

    // pick the group with the min spec virt time
    struct group *min_group = NULL;
    double min_spec_virt_time = INT_MAX;
    struct group *curr_group = gs->group_head;
    while (curr_group) {
        int threads_queued = curr_group->num_threads - num_cores_running(curr_group);
        if (threads_queued > 0) {
            double effective_spec_virt_time = curr_group->spec_virt_time;
            if (curr_group == prev_running_group) effective_spec_virt_time -= GROUP_BONUS;
            if (effective_spec_virt_time < min_spec_virt_time) {
                min_spec_virt_time = effective_spec_virt_time;
                min_group = curr_group;
            }
        }
        curr_group = curr_group->next;
    }

    if (!min_group) {
        // no group to run
        return;
    }

    // assign the next process
    struct process *next_p = min_group->runqueue_head;
    gs->cores[core_id]->current_process = next_p;
    min_group->runqueue_head = next_p->next;
    next_p->next = NULL;

    int group_changed = prev_running_group != min_group;
    trace_schedule(next_p->process_id, next_p->group->group_id, group_changed, core_id);

    // update spec time
    double time_expecting = (double)TICK_LENGTH / next_p->group->weight;
    next_p->group->spec_virt_time += time_expecting;
    struct spec_time *new_spec_time = create_spec_time(time_expecting, core_id);
    struct spec_time *old_spec_head = next_p->group->spec_time_head;
    next_p->group->spec_time_head = new_spec_time;
    new_spec_time->next = old_spec_head;

    // printf("done scheduling core %d\n", core_id);
}

void dequeue(struct process *p, int time_gotten, int core_id) {

    // printf("dequeuing core %d\n", core_id);

    int need_resched = 0;

    // if the process was running, need to reschedule (don't need to account yet, schedule will do that)
    for (int i = 0; i < NUM_CORES; i++) {
        if (gs->cores[i]->current_process == p) {
            need_resched = 1;
            break;
        }
    }

    // if it wasn't running, need to remove it from the group's runqueue
    if (!need_resched) {
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

    // update the group's number of threads
    p->group->num_threads -= 1;

    // need to remove group from global group list if now empty
    if (p->group->num_threads == 0) { 
        struct group *curr_group = gs->group_head;
        if (curr_group == p->group) {
            gs->group_head = curr_group->next;
        } else {
            while (curr_group) {
                if (curr_group->next == p->group) {
                    curr_group->next = curr_group->next->next;
                }
                curr_group = curr_group->next;
            }
        }
        p->group->spec_virt_time = -1;

        // remove speculation if there was any
        gs->cores[core_id]->current_process = NULL;
        struct spec_time *curr_spec_time = p->group->spec_time_head;
        assert(curr_spec_time->next == NULL);
        p->group->spec_time_head = NULL;
        free(curr_spec_time);
    }

    // schedule if need be
    if (need_resched) {
        schedule(core_id, time_gotten, 0);
    }

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
        struct timeval start, end;
        gettimeofday(&start, NULL);

        pthread_mutex_lock(&global_lock);
        schedule(core_id, TICK_LENGTH, 1);
        gettimeofday(&end, NULL);
        long long us_elapsed = (end.tv_sec * 1000000 + end.tv_usec) - (start.tv_sec * 1000000 + start.tv_usec);

        FILE *f = fopen("schedule_time.txt", "a");
        fprintf(f, "%lld\n", us_elapsed);
        fclose(f);
        pthread_mutex_unlock(&global_lock);

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
            pthread_mutex_lock(&global_lock);
            enqueue(p, 1);
            pthread_mutex_unlock(&global_lock);
            trace_enqueue(p->process_id, p->group->group_id, core_id);
            usleep((int)(TICK_LENGTH / 2));
        } else {
            pthread_mutex_lock(&global_lock);
            struct process *p = gs->cores[core_id]->current_process;
            if (!p) {
                pthread_mutex_unlock(&global_lock);
                continue;
            }
            dequeue(p, TICK_LENGTH, core_id);
            pthread_mutex_unlock(&global_lock);
            trace_dequeue(p->process_id, p->group->group_id, core_id);
            p->next = pool;
            pool = p;
            usleep((int)(TICK_LENGTH / 2));
        }
        
    }


}


void main() {
    pthread_mutex_init(&global_lock, NULL);
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

    pthread_t threads[NUM_CORES]; 

    for (int i = 0; i < NUM_CORES; i ++) {
        pthread_create(&threads[i], NULL, run_core, (void*)i);
        usleep(200);
    }

    for (int i = 0; i < NUM_CORES; i++) {
        pthread_join(threads[i], NULL);
    }

}





