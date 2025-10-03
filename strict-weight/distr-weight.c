#define _GNU_SOURCE
#include <stdio.h>
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#include <sched.h>
#include <assert.h>

#define NUM_CORES 56


// this is stupid, but that's ok I am too


// global data structures
struct process {
    int process_id;
    struct group *group;
    struct process *next;
};

struct group {
    int group_id;
    int weight;
    int num_threads;
    struct process *runqueue_head;
    struct group *next;
};

struct global_state {
    int total_weight;
    struct group *group_head;
    struct core_state *cores[NUM_CORES];
};


// local data structures
struct local_group {
    struct group *group;
    double virt_time_gotten;
    double local_weight;
    struct local_group *next;
};

struct core_state {
    int core_id;
    double remaining_capacity; // as a fraction, ie between 0 and 1
    struct process *current_process;
    struct local_group *local_group_head;
};

struct global_state* gs;
pthread_mutex_t global_lock;


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
    g->runqueue_head = NULL;
    g->next = NULL;
    return g;
}

struct local_group *create_local_group(struct group *group, double local_weight) {
    struct local_group *g = malloc(sizeof(struct local_group));
    g->group = group;
    g->local_weight = local_weight;
    g->next = NULL;
    return g;
}


// ================
// HELPER FUNCTIONS
// ================

int compare_groups_by_weight_p_thread(const void *pa, const void *pb) {
    const struct group *a = *(const struct group **)pa;
    const struct group *b = *(const struct group **)pb;
    double a_weight_p_thread = a->num_threads == 0 ? 0.0 : (double)a->weight / (double)a->num_threads;
    double b_weight_p_thread = b->num_threads == 0 ? 0.0 : (double)b->weight / (double)b->num_threads;
    if (a_weight_p_thread > b_weight_p_thread) return -1;
    if (a_weight_p_thread < b_weight_p_thread) return 1;
    return 0;
}


void sort_groups_by_weight_p_thread() {

    // convert to array
    int num_groups = 0;
    struct group *curr_group = gs->group_head;
    while (curr_group) {
        num_groups++;
        curr_group = curr_group->next;
    }

    struct group* groups_arr[num_groups];
    curr_group = gs->group_head;
    for (int i = 0; i < num_groups; i++) {
        groups_arr[i] = curr_group;
        curr_group = curr_group->next;
    }

    // sort (descending weight per thread)
    qsort(groups_arr, num_groups, sizeof(struct group *), compare_groups_by_weight_p_thread);

    // convert back to list
    for (int i = 0; i < num_groups; i++) {
        if (i == 0) {
            gs->group_head = groups_arr[i];
        } else {
            groups_arr[i-1]->next = groups_arr[i];
        }
    }
    groups_arr[num_groups-1]->next = NULL;

}

void free_all_local_groups() {
    for (int i = 0; i < NUM_CORES; i++) {
        struct local_group *curr_local_group = gs->cores[i]->local_group_head;
        while (curr_local_group) {
            struct local_group *next_local_group = curr_local_group->next;
            free(curr_local_group);
            curr_local_group = next_local_group;
        }
        gs->cores[i]->local_group_head = NULL;
        gs->cores[i]->remaining_capacity = 1;
    }
}

int p_in_rq(struct process *p) {
    struct process *curr_head = p->group->runqueue_head;
    while (curr_head) {
        if (curr_head->process_id == p->process_id) {
            return 1;
        }
        curr_head = curr_head->next;
    }
    return 0;
}

int group_in_gs(struct group *g) {
    struct group *curr_group = gs->group_head;
    while (curr_group) {
        if (curr_group == g) {
            return 1;
        }
        curr_group = curr_group->next;
    }
    return 0;
}


void re_enq_running_processes() {

    for (int i = 0; i < NUM_CORES; i++) {
        struct process *curr_process = gs->cores[i]->current_process;
        if (curr_process) {
            assert(!p_in_rq(curr_process));
            // add it to the end of the runqueue
            struct process *curr_head = curr_process->group->runqueue_head;
            if (!curr_head) {
                curr_process->group->runqueue_head = curr_process;
                curr_process->next = NULL;
                gs->cores[i]->current_process = NULL;
                continue;
            }
            while (curr_head->next) {
                curr_head = curr_head->next;
            }
            curr_head->next = curr_process;
            curr_process->next = NULL;
            gs->cores[i]->current_process = NULL;
        }
    }
    
}


void print_global_state() {
    printf("global state:\n");
    printf("total weight: %d\n", gs->total_weight);
    struct group *curr_group = gs->group_head;
    while (curr_group) {
        printf("group %d, weight %d, num threads %d\n", curr_group->group_id, curr_group->weight, curr_group->num_threads);
        curr_group = curr_group->next;
    }

    printf("cores:\n");
    for (int i = 0; i < NUM_CORES; i++) {
        printf("core %d:\n", i);
        struct local_group *curr_local_group = gs->cores[i]->local_group_head;
        while (curr_local_group) {
            printf("  local group %d, weight %f\n", curr_local_group->group->group_id, curr_local_group->local_weight);
            curr_local_group = curr_local_group->next;
        }
    }

}

void write_global_state(FILE *f) {
    fprintf(f, "global state:\n");
    fprintf(f, "total weight: %d\n", gs->total_weight);
    struct group *curr_group = gs->group_head;
    while (curr_group) {
        fprintf(f, "group %d, weight %d, num threads %d\n", curr_group->group_id, curr_group->weight, curr_group->num_threads);
        struct process *curr_process = curr_group->runqueue_head;
        while (curr_process) {
            fprintf(f, "  process %d\n", curr_process->process_id);
            curr_process = curr_process->next;
        }
        curr_group = curr_group->next;
    }

    fprintf(f, "cores:\n");
    for (int i = 0; i < NUM_CORES; i++) {
        fprintf(f, "core %d:\n", i);
        struct local_group *curr_local_group = gs->cores[i]->local_group_head;
        while (curr_local_group) {
            fprintf(f, "  local group %d, weight %f\n", curr_local_group->group->group_id, curr_local_group->local_weight);
            curr_local_group = curr_local_group->next;
        }
    }

}

// ================
// MAIN FUNCTIONS
// ================


void distribute_weight() {

    int weight_left = gs->total_weight;
    int cores_left = NUM_CORES;
    double curr_w_p_core, curr_w_p_thread;

    sort_groups_by_weight_p_thread();

    int curr_core_assigning = 0;

    struct group *curr_group = gs->group_head;
    while (1) {

        while (curr_group) {

            curr_w_p_core = (double)weight_left / (double)cores_left;
            curr_w_p_thread = (double)curr_group->weight / (double)curr_group->num_threads;
    
            if (curr_w_p_thread <= curr_w_p_core) {
                break;
            }

            int cores_group_should_get = curr_group->num_threads;

            // actually assign to a core
            int cores_to_assign = cores_group_should_get;
            // printf("group %d, cores to assign %d\n", curr_group->group->group_id, cores_to_assign);
            for (int i=curr_core_assigning; i < NUM_CORES; i++) {
                struct core_state *curr_core = gs->cores[i];
                if (cores_to_assign == 0) break;
                if (curr_core->remaining_capacity == 1) {
                    curr_core->remaining_capacity = 0;
                    struct local_group *new_local_grp = create_local_group(curr_group, 1);
                    curr_core->local_group_head = new_local_grp;
                    curr_core_assigning +=1;
                    cores_to_assign -= 1;
                }
            }

            cores_left -= cores_group_should_get;
            weight_left -= curr_group->weight;
            curr_group = curr_group->next;
        }

        // all of the groups were underserved, ie we have idle cores left
        break;

    }
    
    // now only have groups with enough threads left
    while (curr_group) {

        double cores_group_should_get = (double)(curr_group->weight) / curr_w_p_core;
        double points_to_assign = cores_group_should_get;
        // printf("group %d, points to assign %f\n", curr_group->group->group_id, points_to_assign);
        for (int i=curr_core_assigning; i < NUM_CORES; i++) {
            struct core_state *curr_core = gs->cores[i];
            if (points_to_assign == 0) break;
            if (curr_core->remaining_capacity > 0) {
                double local_points_to_assign;
                if (points_to_assign < curr_core->remaining_capacity) {
                    local_points_to_assign = points_to_assign;
                } else {
                    local_points_to_assign = curr_core->remaining_capacity;
                }

                curr_core->remaining_capacity = curr_core->remaining_capacity - local_points_to_assign;

                struct local_group *new_local_grp = create_local_group(curr_group, local_points_to_assign);
                struct local_group *curr_head = curr_core->local_group_head;
                curr_core->local_group_head = new_local_grp;
                new_local_grp->next = curr_head;

                points_to_assign -= local_points_to_assign;
            }
        }

        curr_group = curr_group->next;
    }

}


void schedule(int core_id, int time_passed) {

    // printf("scheduling core %d\n", core_id);

    // update timing
    struct process *running_process = gs->cores[core_id]->current_process;
    if (running_process) {
        struct local_group *curr_group = gs->cores[core_id]->local_group_head;
        while (curr_group) {
            if (curr_group->group == running_process->group) {
                curr_group->virt_time_gotten += (double)time_passed / curr_group->local_weight;
                break;
            }
            curr_group = curr_group->next;
        }
    }

    // re-enq the running process
    if (running_process) {
        // add it to the end of the runqueue
        struct process *curr_head = running_process->group->runqueue_head;
        if (!curr_head) {
            running_process->group->runqueue_head = running_process;
            running_process->next = NULL;
        } else {
            while (curr_head->next) {
                curr_head = curr_head->next;
            }
            curr_head->next = running_process;
            running_process->next = NULL;
        }
    }

    // choose the local group with min virt time gotten
    struct local_group *min_local_group = NULL;
    struct local_group *curr_local_group = gs->cores[core_id]->local_group_head;
    while (curr_local_group) {
        if (min_local_group == NULL || curr_local_group->virt_time_gotten < min_local_group->virt_time_gotten) {
            min_local_group = curr_local_group;
        }
        curr_local_group = curr_local_group->next;
    }

    if (!min_local_group) {
        assert(gs->cores[core_id]->current_process == NULL);
        // printf("done scheduling core %d\n", core_id);
        return;
    }

    struct process *next_p = min_local_group->group->runqueue_head;
    assert(next_p); // the algorithm should only assign groups that have threads left
    min_local_group->group->runqueue_head = next_p->next;
    next_p->next = NULL;

    gs->cores[core_id]->current_process = next_p;
    assert(!p_in_rq(next_p));

    // printf("done scheduling core %d\n", core_id);

}



// add to group
void enqueue(struct process *p) {

    // printf("enqueuing p %d\n", p->process_id);

    if (!group_in_gs(p->group)) {
        assert(p->group->num_threads == 0);
        struct group *curr_group = gs->group_head;
        while (curr_group->next) {
            curr_group = curr_group->next;
        }
        curr_group->next = p->group;
        p->group->next = NULL;
        gs->total_weight += p->group->weight;
    }


    assert(group_in_gs(p->group));

    assert(!p_in_rq(p));

    free_all_local_groups();
    re_enq_running_processes();

    // add the new process to its group
    struct process *curr_head = p->group->runqueue_head;
    p->group->runqueue_head = p;
    p->next = curr_head;
    p->group->num_threads += 1;

    distribute_weight();

}

void dequeue(struct process *p) {

    // printf("dequeuing p %d\n", p->process_id);
    
    free_all_local_groups();
    re_enq_running_processes();

    assert(p_in_rq(p));

    // remove the process from its group
    struct process *curr_p = p->group->runqueue_head;

    assert(curr_p);
    int done = 0;
    if (curr_p->process_id == p->process_id) {
        done = 1;
        p->group->runqueue_head = curr_p->next;
    } else {
        struct process *prev = curr_p;
        curr_p = curr_p->next;
        while (curr_p) {
            if (curr_p->process_id == p->process_id) {
                prev->next = curr_p->next;
                done = 1;
                break;
            }
            prev = curr_p;
            curr_p = curr_p->next;
        }
    }
    assert(done);
    
    p->next = NULL;
    p->group->num_threads -= 1;

    assert(p->group->num_threads >= 0);

    assert(!p_in_rq(p));

    if (p->group->num_threads == 0) {
        // remove the group from the global rq
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
        gs->total_weight -= p->group->weight;
    }

    if (p->group->num_threads == 0) {
        assert(!group_in_gs(p->group));
    }

    distribute_weight();

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
        schedule(core_id, 4000);
        gettimeofday(&end, NULL);
        long long us_elapsed = (end.tv_sec * 1000000 + end.tv_usec) - (start.tv_sec * 1000000 + start.tv_usec);

        FILE *f = fopen("schedule_time.txt", "a");
        fprintf(f, "%lld\n", us_elapsed);
        fclose(f);

        f = fopen("schedule_log.txt", "a");
        fprintf(f, "scheduled core %d\n", core_id);
        write_global_state(f);
        fprintf(f, "\n");
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
            FILE *f = fopen("schedule_log.txt", "a");
            fprintf(f, "enqueued p %d on core %d\n", p->process_id, core_id);
            write_global_state(f);
            fprintf(f, "\n");
            fclose(f);
            pthread_mutex_unlock(&global_lock);
            usleep(2000);
        } else {
            pthread_mutex_lock(&global_lock);
            struct process *p = gs->cores[core_id]->current_process;
            if (!p) {
                pthread_mutex_unlock(&global_lock);
                continue;
            }
            dequeue(p);
            
            FILE *f = fopen("schedule_log.txt", "a");
            fprintf(f, "dequeued p %d on core %d\n", p->process_id, core_id);
            write_global_state(f);
            fprintf(f, "\n");
            fclose(f);
            pthread_mutex_unlock(&global_lock);
            p->next = pool;
            pool = p;
            usleep(2000);
        }
        
    }


}


void main() {
    pthread_mutex_init(&global_lock, NULL);

    FILE *f = fopen("schedule_time.txt", "w");
    fclose(f);

    f = fopen("schedule_log.txt", "w");
    fclose(f);

    int num_groups = 10;
    int num_threads_p_group = 20;

    gs = malloc(sizeof(struct global_state));
    gs->total_weight = 0;
    gs->group_head = NULL;
    for (int i = 0; i < NUM_CORES; i++) {
        gs->cores[i] = malloc(sizeof(struct core_state));
        gs->cores[i]->core_id = i;
        gs->cores[i]->local_group_head = NULL;
        gs->cores[i]->remaining_capacity = 1;
    }

    for (int i = 0; i < num_groups; i++) {
        struct group *g = create_group(i, 10);
        // attach group to the head
        struct group *curr_head = gs->group_head;
        gs->group_head = g;
        g->next = curr_head;
        gs->total_weight += g->weight;
        for (int j = 0; j < num_threads_p_group; j++) {
            struct process *p = create_process(i*num_threads_p_group+j, g);
            enqueue(p);
        }
    }

    // print_global_state();


    pthread_t threads[NUM_CORES]; 

    for (int i = 0; i < NUM_CORES; i ++) {
        pthread_create(&threads[i], NULL, run_core, (void*)i);
    }

    for (int i = 0; i < NUM_CORES; i++) {
        pthread_join(threads[i], NULL);
    }

}







// when do core assignments change? 
// - positive (ie new work that needs to potentially take over from existing work)
//      - when underserved groups get a new thread
//      - when a new group starts
// - negative (ienew space is created where existing work can be moved to)
//      - when overserved group loses enough threads to become underserved
//      - when a group stops/is removed (is that really different from the first?)

