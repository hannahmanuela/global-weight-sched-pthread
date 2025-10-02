#define _GNU_SOURCE
#include <sys/time.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sched.h>

// the global data structures that we need access to

// TODO: 
// - implement a "process" that actually runs on the core? why can't the thing just sleep between scheduling? is it important that things actually run in between?
// - metric of success (pct time core spent running vs in lock?)
// - need a way to interrupt threads if schedule() on coreX finds that the thing that needs to run on coreY is different (general provlem with global schedulers)
// - support weights for fractional cores? (will need to track running time etc then :/)

pthread_mutex_t global_lock;
struct global_state* gs;

#define NUM_CORES 56

struct group {
    int id;
    int weight;
    int cores_should_get;
    struct process *runqueue_head;
    struct group *next;
};

struct process {
    int id;
    struct group *group;
    struct process *next;
    int core_running_on;
};

struct core_state {
    int core_id;
    struct process *current_process;
};

struct global_state {
    int total_weight;
    struct group *group_head;
    struct core_state *cores[NUM_CORES];
};

struct process *create_process(int id, struct group *group, struct process *next) {
    struct process *p = malloc(sizeof(struct process));
    p->id = id;
    p->group = group;
    p->next = next;
    p->core_running_on = -1;
    return p;
}

struct group *create_group(int id, int weight) {
    struct group *g = malloc(sizeof(struct group));
    g->id = id;
    g->weight = weight;
    g->cores_should_get = 0; // has no threads yet
    g->runqueue_head = NULL;
    g->next = NULL;
    return g;
}


void print_global_state() {
    printf("global state:\n");
    printf("total weight: %d\n", gs->total_weight);
    struct group *curr_group = gs->group_head;
    while (curr_group) {
        printf("group %d, weight %d, cores should get %d\n", curr_group->id, curr_group->weight, curr_group->cores_should_get);
        struct process *curr_process = curr_group->runqueue_head;
        while (curr_process) {
            printf("  process %d, running on %d\n", curr_process->id, curr_process->core_running_on);
            curr_process = curr_process->next;
        }
        curr_group = curr_group->next;
    }

    printf("cores:\n");
    for (int i = 0; i < NUM_CORES; i++) {
        printf("core %d: %d\n", i, gs->cores[i]->current_process ? gs->cores[i]->current_process->id : -1);
    }
}



void schedule() {

    // re-enq the old currents 
    for (int i = 0; i < NUM_CORES; i++) {
        struct process *running_process = gs->cores[i]->current_process;
        if (running_process) {
            running_process->core_running_on = -1;
            gs->cores[i]->current_process = NULL;

            // printf("re-enqing process %d\n", running_process->id);
            struct process *curr_head = running_process->group->runqueue_head;
            if (!curr_head) {
                // just create new node
                running_process->group->runqueue_head = running_process;
                running_process->next = NULL; // just in case
                continue;
            }
            while (curr_head->next) {
                curr_head = curr_head->next;
            }
            curr_head->next = running_process;
            running_process->next = NULL; // just in case
        }
    }


    // for every group, calculate number of cores that it should get based on its weight
    struct group *curr_group = gs->group_head;
    int n_cores_assigned = 0;
    while (curr_group) {
        for (int i = 0; i < curr_group->cores_should_get; i++) {
            if (!curr_group->runqueue_head) {
                // printf("no threads left to run for group %d\n", curr_group->id);
                // no threads left to run
                break;
            }
            struct process* run_next = curr_group->runqueue_head;
            gs->cores[n_cores_assigned]->current_process = run_next;
            curr_group->runqueue_head = run_next->next;
            run_next->next = NULL;
            run_next->core_running_on = n_cores_assigned;
            n_cores_assigned ++;
            // printf("running process %d on core %d\n", run_next->id, n_cores_assigned);
        }
        curr_group = curr_group->next;
    }


    // some group does not have enough threads to use all of its weight
    // work conserving: let anything go beyond what its weight allows rather than have the core go idle
    // TODO: choose what gets to do this less crudely
    if (n_cores_assigned < NUM_CORES) {
        // pick something random to over-use above its weight
        while (curr_group && n_cores_assigned < NUM_CORES) {
            if (!curr_group->runqueue_head) {
                // no threads left to run
                continue;
            }
            struct process* run_next = curr_group->runqueue_head;
            gs->cores[n_cores_assigned]->current_process = run_next;
            curr_group->runqueue_head = run_next->next;
            run_next->next = NULL;
            run_next->core_running_on = n_cores_assigned;
            n_cores_assigned ++;
            // printf("running process %d on core %d\n", run_next->id, n_cores_assigned);
        }
    }
    

}

// assumes that p was running somewhere 
void dequeue(struct process *p) {

    // printf("deqing process %d from core %d\n", p->id, p->core_running_on);

    struct process *next_p = p->group->runqueue_head;
    if (next_p) {
        // printf("there's a next process\n");
        gs->cores[p->core_running_on]->current_process = next_p;
        p->group->runqueue_head = next_p->next;
        next_p->next = NULL;
        next_p->core_running_on = p->core_running_on;
        p->core_running_on = -1;
        return;
    }


last_thread:
    // this was the last thread of this group
    gs->cores[p->core_running_on]->current_process = NULL;
    p->core_running_on = -1;
    gs->total_weight -= p->group->weight;
    p->group->next = NULL;

    // adjust cores_should_get
    struct group *curr_group = gs->group_head;
    if (curr_group == p->group) {
        gs->group_head = gs->group_head->next;
    } else {
        while (curr_group) {
            if (curr_group->next == p->group) {
                curr_group->next = curr_group->next->next;
                break;
            }
            curr_group = curr_group->next;
        }
    }
    
    // Recalculate cores_should_get for all remaining groups
    curr_group = gs->group_head;
    while (curr_group) {
        curr_group->cores_should_get = (int)(curr_group->weight * NUM_CORES / gs->total_weight);
        curr_group = curr_group->next;
    }

    schedule();

    return;
}


void enqueue(struct process *p) {

    // if this is the first thread in the group is already running
    int found = 0;
    struct group *curr_group = gs->group_head;
    if (curr_group == p->group) {
        found = 1;
    }
    while (curr_group && !found) {
        if (curr_group->next == p->group) {
            found = 1;
        }
        curr_group = curr_group->next;
    }

    if (!found) {
        p->group->next = gs->group_head;
        gs->group_head = p->group;

        p->group->runqueue_head = p;
        p->next = NULL;

        gs->total_weight += p->group->weight;

        struct group *curr_group = gs->group_head;
        while (curr_group) {
            curr_group->cores_should_get = (int)(curr_group->weight * NUM_CORES / gs->total_weight );
            curr_group = curr_group->next;
        }
    } else {
        struct process *curr_head = p->group->runqueue_head;
        if (!curr_head) {
            p->group->runqueue_head = p;
            p->next = NULL;
        } else {
            while (curr_head->next) {
                curr_head = curr_head->next;
            }
            curr_head->next = p;
            p->next = NULL;
        }
    }

    schedule();
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
        schedule();
        gettimeofday(&end, NULL);
        long long us_elapsed = (end.tv_sec * 1000000 + end.tv_usec) - (start.tv_sec * 1000000 + start.tv_usec);
        FILE *f = fopen("schedule_time.txt", "a");
        fprintf(f, "%lld\n", us_elapsed);
        fclose(f);
        pthread_mutex_unlock(&global_lock);


        // randomly choose to: "run" for the full tick, "enq" a new process, or "deq" something
        int choice = rand() % 3;
        if (choice == 0) {
            usleep(4000); // just run
        } else if (choice == 1) {
            // pick an exisitng process from the pool?
            struct process *p = pool;
            if (!p) {
                continue;
            }
            pool = p->next;
            p->next = NULL;
            pthread_mutex_lock(&global_lock);
            enqueue(p);
            pthread_mutex_unlock(&global_lock);
        } else {
            pthread_mutex_lock(&global_lock);
            struct process *p = gs->cores[core_id]->current_process;
            if (!p) {
                pthread_mutex_unlock(&global_lock);
                continue;
            }
            dequeue(p);
            pthread_mutex_unlock(&global_lock);
            p->next = pool;
            pool = p;
        }
        
    }


}


void main() {
    pthread_mutex_init(&global_lock, NULL);

    // empty file
    FILE *f = fopen("schedule_time.txt", "w");
    fclose(f);

    gs = malloc(sizeof(struct global_state));
    gs->total_weight = 0;
    gs->group_head = NULL;
    for (int i = 0; i < NUM_CORES; i++) {
        gs->cores[i] = malloc(sizeof(struct core_state));
        gs->cores[i]->core_id = i;
        gs->cores[i]->current_process = NULL;
    }


    // create a couple groups and a bunch of processes
    int num_groups = 10;
    int num_processes = 10;
    struct group *prev_group = NULL;
    for (int i = 0; i < num_groups; i++) {
        struct group *g = create_group(i, 1);
        for (int j = 0; j < num_processes; j++) {
            struct process *p = create_process(i*num_processes+j, g, NULL);
            enqueue(p);
        }
    }

    pthread_t threads[NUM_CORES]; 

    for (int i = 0; i < NUM_CORES; i ++) {
        pthread_create(&threads[i], NULL, run_core, (void*)i);
    }

    for (int i = 0; i < NUM_CORES; i++) {
        pthread_join(threads[i], NULL);
    }

}



