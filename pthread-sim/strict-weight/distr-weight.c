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
#define MS_TO_US 1000
#define TICK_MS 8


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

int p_in_local_group(struct process *p, int core_id) {

    int found = 0;

    if (gs->cores[core_id]->current_process) {
        struct local_group *curr_group = gs->cores[core_id]->local_group_head;
        while (curr_group) {
            if (curr_group->group == p->group) {
                found = 1;
                break;
            }
            curr_group = curr_group->next;
        }
    }

    return found;
}


void re_enq_running_process(int core_id) {

    struct process *curr_process = gs->cores[core_id]->current_process;
    if (curr_process) {
        assert(!p_in_rq(curr_process));
        // add it to the end of the runqueue
        struct process *curr_head = curr_process->group->runqueue_head;
        if (!curr_head) {
            curr_process->group->runqueue_head = curr_process;
            curr_process->next = NULL;
            gs->cores[core_id]->current_process = NULL;
            return;
        }
        while (curr_head->next) {
            curr_head = curr_head->next;
        }
        curr_head->next = curr_process;
        curr_process->next = NULL;
        gs->cores[core_id]->current_process = NULL;
    }
    
}

double sum_weight_cores_running(struct group *group) {
    double num_cores_running = 0;
    for (int i = 0; i < NUM_CORES; i++) {
        struct local_group *curr_local_group = gs->cores[i]->local_group_head;
        while (curr_local_group) {
            if (curr_local_group->group == group) {
                num_cores_running += curr_local_group->local_weight;
            }
            curr_local_group = curr_local_group->next;
        }
    }
    return num_cores_running;
}

int qlen(struct group *group) {
    struct process *curr_process = group->runqueue_head;
    int len = 0;
    while (curr_process) {
        len++;
        curr_process = curr_process->next;
    }
    return len;
}

void print_global_state(struct global_state *gs_to_print) {
    printf("global state:\n");
    printf("total weight: %d\n", gs_to_print->total_weight);
    struct group *curr_group = gs_to_print->group_head;
    while (curr_group) {
        printf("group %d, weight %d, num threads %d, q_len %d\n", curr_group->group_id, curr_group->weight, curr_group->num_threads, qlen(curr_group));
        curr_group = curr_group->next;
    }

    printf("cores:\n");
    for (int i = 0; i < NUM_CORES; i++) {
        printf("core %d current process %d (grp %d):\n", i, gs_to_print->cores[i]->current_process ? gs_to_print->cores[i]->current_process->process_id : -1, gs_to_print->cores[i]->current_process ? gs_to_print->cores[i]->current_process->group->group_id : -1);
        struct local_group *curr_local_group = gs_to_print->cores[i]->local_group_head;
        while (curr_local_group) {
            printf("  local group %d, weight %f\n", curr_local_group->group->group_id, curr_local_group->local_weight);
            curr_local_group = curr_local_group->next;
        }
    }

}

struct global_state *copy_global_state() {
    struct global_state *new_gs = malloc(sizeof(struct global_state));
    new_gs->total_weight = gs->total_weight;

    new_gs->group_head = gs->group_head; // we don't touch the list of groups

    for (int i = 0; i < NUM_CORES; i++) {
        new_gs->cores[i] = malloc(sizeof(struct core_state));
        new_gs->cores[i]->core_id = i;
        new_gs->cores[i]->local_group_head = NULL;
        new_gs->cores[i]->remaining_capacity = 1;
        new_gs->cores[i]->current_process = NULL;
        // new_gs->cores[i]->remaining_capacity = gs->cores[i]->remaining_capacity;
        // struct local_group *curr_local_group = gs->cores[i]->local_group_head;
        // struct local_group *curr_new_local_group = NULL;
        
        // if (curr_local_group) {
        //     new_gs->cores[i]->local_group_head = create_local_group(curr_local_group->group, curr_local_group->local_weight);
        //     curr_new_local_group = new_gs->cores[i]->local_group_head;
        //     curr_local_group = curr_local_group->next;
            
        //     while (curr_local_group) {
        //         curr_new_local_group->next = create_local_group(curr_local_group->group, curr_local_group->local_weight);
        //         curr_local_group = curr_local_group->next;
        //         curr_new_local_group = curr_new_local_group->next;
        //     }
        // } else {
        //     new_gs->cores[i]->local_group_head = NULL;
        // }
    }

    return new_gs;
}

void free_global_state(struct global_state *gs_to_free) {

    // free cores
    for (int i = 0; i < NUM_CORES; i++) {
        free(gs_to_free->cores[i]);
    }

    // free the pointer itself
    free(gs_to_free);
}

void trace_wakeup(int process_id, int num_cores_changed) {
    pthread_mutex_lock(&log_lock);
    FILE *f = fopen("event_log.txt", "a");
    struct timeval curr;
    gettimeofday(&curr, NULL);
    long long ms_tod = (curr.tv_sec * 1000 + curr.tv_usec / 1000);
    fprintf(f, "%lld wakeup %d, caused changes %d\n", ms_tod, process_id, num_cores_changed);
    fclose(f);
    pthread_mutex_unlock(&log_lock);
}

void trace_exit(int process_id, int num_cores_changed) {
    pthread_mutex_lock(&log_lock);
    FILE *f = fopen("event_log.txt", "a");
    struct timeval curr;
    gettimeofday(&curr, NULL);
    long long ms_tod = (curr.tv_sec * 1000 + curr.tv_usec / 1000);
    fprintf(f, "%lld exit %d, caused changes %d\n", ms_tod, process_id, num_cores_changed);
    fclose(f);
    pthread_mutex_unlock(&log_lock);
}

void trace_schedule(int process_id, int core_id) {
    pthread_mutex_lock(&log_lock);
    FILE *f = fopen("event_log.txt", "a");
    struct timeval curr;
    gettimeofday(&curr, NULL);
    long long ms_tod = (curr.tv_sec * 1000 + curr.tv_usec / 1000);
    fprintf(f, "%lld schedule %d %d\n", ms_tod, core_id, process_id);
    fclose(f);
    pthread_mutex_unlock(&log_lock);
}

// ================
// MAIN FUNCTIONS
// ================


void distribute_weight(struct global_state *new_gs) {

    int weight_left = new_gs->total_weight;
    int cores_left = NUM_CORES;
    double curr_w_p_core, curr_w_p_thread;

    sort_groups_by_weight_p_thread();

    int curr_core_assigning = 0;

    struct group *curr_group = new_gs->group_head;
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
                struct core_state *curr_core = new_gs->cores[i];
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
            struct core_state *curr_core = new_gs->cores[i];
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

int distribute_enforce_weight() {

    struct global_state *new_gs = copy_global_state();

    // printf("======= BEFORE =======\n");
    // print_global_state(gs);

    distribute_weight(new_gs);

    // printf("========= AFTER =========\n");
    // print_global_state(new_gs);

    int cores_changed[NUM_CORES];
    int num_cores_changed = 0;
    for (int i = 0; i < NUM_CORES; i++) {
        cores_changed[i] = 0;
    }

    // look at difference for each core, notify those that have changed? update those that changed in the actual gs?
    for (int i = 0; i < NUM_CORES; i++) {
        struct local_group *old_local_group_head = gs->cores[i]->local_group_head;
        struct local_group *new_local_group_head = new_gs->cores[i]->local_group_head;
        int changed = 0;

        
        if (!old_local_group_head && new_local_group_head || old_local_group_head && !new_local_group_head) { // Case 1: old is empty, new is not
            changed = 1;
        } else if (old_local_group_head && new_local_group_head) { // Case 2: both non-empty, check for differences
            // Check if every old_local_group exists in new_local_group
            struct local_group *curr_old = old_local_group_head;
            while (curr_old) {
                int match_found = 0;
                struct local_group *curr_new = new_local_group_head;
                while (curr_new) {
                    if (curr_old->group == curr_new->group && curr_old->local_weight == curr_new->local_weight) {
                        match_found = 1;
                        break;
                    }
                    curr_new = curr_new->next;
                }
                if (!match_found) {
                    changed = 1;
                    break;
                }
                curr_old = curr_old->next;
            }
            // Also check if every new_local_group exists in old_local_group (to catch additions)
            if (!changed) {
                struct local_group *curr_new = new_local_group_head;
                while (curr_new) {
                    int match_found = 0;
                    struct local_group *curr_old = old_local_group_head;
                    while (curr_old) {
                        if (curr_new->group == curr_old->group && curr_new->local_weight == curr_old->local_weight) {
                            match_found = 1;
                            break;
                        }
                        curr_old = curr_old->next;
                    }
                    if (!match_found) {
                        changed = 1;
                        break;
                    }
                    curr_new = curr_new->next;
                }
            }
        }

        if (changed) {
            cores_changed[i] = 1;
            num_cores_changed++;
        }
    }

    for (int i = 0; i < NUM_CORES; i++) {
        if (cores_changed[i]) {
            // Free the old local_group list in gs->cores[i]
            struct local_group *curr = gs->cores[i]->local_group_head;
            while (curr) {
                struct local_group *next = curr->next;
                free(curr);
                curr = next;
            }
            // Overwrite with the new local_group list from new_gs
            gs->cores[i]->local_group_head = new_gs->cores[i]->local_group_head;
            new_gs->cores[i]->local_group_head = NULL; // Prevent double free if new_gs is later freed
        }
    }

    free_global_state(new_gs);

    return num_cores_changed;

}


void schedule(int core_id, int time_passed) {

    // printf("scheduling core %d\n", core_id);
    struct process *running_process = gs->cores[core_id]->current_process;

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

    // update timing if required (don't if we were running a process from a group that we are no longer in charge of, b/c an enq/deq moved local groups around)
    if (running_process && p_in_local_group(running_process, core_id)) {
        struct local_group *curr_group = gs->cores[core_id]->local_group_head;
        while (curr_group) {
            if (curr_group->group == running_process->group) {
                curr_group->virt_time_gotten += (double)time_passed / curr_group->local_weight;
                break;
            }
            curr_group = curr_group->next;
        }
    }

    gs->cores[core_id]->current_process = NULL;

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
        // printf("done scheduling core %d\n", core_id);    
        trace_schedule(-1, core_id);
        return;
    }

    struct process *next_p = min_local_group->group->runqueue_head;
    if (!next_p) {
        // this is sometimes happening because cores might still be running processes from the old groups even after re-assignment, so if a new core tries to schedule its new group it might not find a process
        printf("no next process for core %d group %d\n", core_id, min_local_group->group->group_id);
        print_global_state(gs);
    }
    assert(next_p); // the algorithm should only assign groups that have threads left
    min_local_group->group->runqueue_head = next_p->next;
    next_p->next = NULL;

    gs->cores[core_id]->current_process = next_p;
    assert(!p_in_rq(next_p));

    trace_schedule(next_p->process_id, core_id);

    // printf("done scheduling core %d\n", core_id);

}



// add to group
int enqueue(struct process *p) {

    // printf("enqueuing p %d\n", p->process_id);

    if (!group_in_gs(p->group)) {
        printf("enqueuing group %d\n", p->group->group_id);
        assert(p->group->num_threads == 0);
        struct group *curr_group = gs->group_head;
        if (!curr_group) {
            gs->group_head = p->group;
        } else {
            while (curr_group->next) {
                curr_group = curr_group->next;
            }
            curr_group->next = p->group;
        }
        p->group->next = NULL;
        gs->total_weight += p->group->weight;
    }


    assert(group_in_gs(p->group));

    assert(!p_in_rq(p));

    // add the new process to its group
    struct process *curr_head = p->group->runqueue_head;
    p->group->runqueue_head = p;
    p->next = curr_head;
    p->group->num_threads += 1;

    // umm what is this doing? it has no way of effectuating the changes it knows it needs to make?
    return distribute_enforce_weight();

}

int dequeue(struct process *p, int core_id) {

    // printf("dequeuing p %d\n", p->process_id);
    
    re_enq_running_process(core_id);

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

    return distribute_enforce_weight();

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
        schedule(core_id, MS_TO_US*TICK_MS);
        gettimeofday(&end, NULL);
        long long us_elapsed = (end.tv_sec * 1000000 + end.tv_usec) - (start.tv_sec * 1000000 + start.tv_usec);

        FILE *f = fopen("schedule_time.txt", "a");
        fprintf(f, "%lld\n", us_elapsed);
        fclose(f);
        pthread_mutex_unlock(&global_lock);

        usleep(TICK_MS * MS_TO_US);

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
            int num_cores_changed = enqueue(p);
            pthread_mutex_unlock(&global_lock);
            trace_wakeup(p->process_id, num_cores_changed);
            usleep((int)(TICK_MS * MS_TO_US / 2));
        } else {
            pthread_mutex_lock(&global_lock);
            struct process *p = gs->cores[core_id]->current_process;
            if (!p) {
                pthread_mutex_unlock(&global_lock);
                continue;
            }
            int num_cores_changed = dequeue(p, core_id);
            pthread_mutex_unlock(&global_lock);
            trace_exit(p->process_id, num_cores_changed);
            p->next = pool;
            pool = p;
            usleep((int)(TICK_MS * MS_TO_US / 2));
        }
        
    }


}


void main() {

    struct sched_param sched_param;
    sched_param.sched_priority = 99;

    sched_setscheduler(0, SCHED_FIFO, &sched_param);

    srand(123);

    pthread_mutex_init(&global_lock, NULL);
    pthread_mutex_init(&log_lock, NULL);

    FILE *f = fopen("event_log.txt", "w");
    fclose(f);

    f = fopen("schedule_time.txt", "w");
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
        gs->cores[i]->current_process = NULL;
    }

    for (int i = 0; i < num_groups; i++) {
        struct group *g = create_group(i, 10);
        for (int j = 0; j < num_threads_p_group; j++) {
            struct process *p = create_process(i*num_threads_p_group+j, g);
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







// when do core assignments change? 
// - positive (ie new work that needs to potentially take over from existing work)
//      - when underserved groups get a new thread
//      - when a new group starts
// - negative (ienew space is created where existing work can be moved to)
//      - when overserved group loses enough threads to become underserved
//      - when a group stops/is removed (is that really different from the first?)
