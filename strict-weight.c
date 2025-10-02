#include <stdio.h>
#include <math.h>
#include <pthread.h>
#include <stdlib.h>

#define NUM_CORES 56


// this is stupid, but that's ok I am too


// global data structures
struct process {
    int process_id;
    int group_id;
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
    int group_id;
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

struct process *create_process(int id, int group_id) {
    struct process *p = malloc(sizeof(struct process));
    p->process_id = id;
    p->group_id = group_id;
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

struct local_group *create_local_group(int group_id, double local_weight) {
    struct local_group *g = malloc(sizeof(struct local_group));
    g->group_id = group_id;
    g->local_weight = local_weight;
    g->next = NULL;
    return g;
}

int compare_groups_by_weight_p_thread(const void *pa, const void *pb) {
    const struct group *a = *(const struct group **)pa;
    const struct group *b = *(const struct group **)pb;
    double a_weight_p_thread = (double)a->weight / (double)a->num_threads;
    double b_weight_p_thread = (double)b->weight / (double)b->num_threads;
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

    printf("num groups %d\n", num_groups);

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
                printf("breaking, curr group %d\n", curr_group->group_id);
                break;
            }

            int cores_group_should_get = curr_group->num_threads;
            printf("group %d, curr w p thread %f, curr w p core %f, should get %d cores\n", curr_group->group_id, curr_w_p_thread, curr_w_p_core, cores_group_should_get);

            // actually assign to a core
            int cores_to_assign = cores_group_should_get;
            for (int i=curr_core_assigning; i < NUM_CORES; i++) {
                struct core_state *curr_core = gs->cores[i];
                if (curr_core->remaining_capacity == 1) {
                    curr_core->remaining_capacity == 0;
                        struct local_group *new_local_grp = create_local_group(curr_group->group_id, 1);
                    curr_core->local_group_head = new_local_grp;
                    curr_core_assigning +=1;
                    cores_to_assign -= 1;
                    if (cores_to_assign == 0) break;
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
        printf("group %d, curr w p core %f, should get %f cores\n", curr_group->group_id, curr_w_p_core, cores_group_should_get);
        double points_to_assign = cores_group_should_get;
        for (int i=curr_core_assigning; i < NUM_CORES; i++) {
            struct core_state *curr_core = gs->cores[i];
            if (curr_core->remaining_capacity > 0) {
                double local_points_to_assign;
                if (points_to_assign < curr_core->remaining_capacity) {
                    local_points_to_assign = points_to_assign;
                } else {
                    local_points_to_assign = curr_core->remaining_capacity;
                }

                curr_core->remaining_capacity = curr_core->remaining_capacity - local_points_to_assign;

                struct local_group *new_local_grp = create_local_group(curr_group->group_id, local_points_to_assign);
                struct local_group *curr_head = curr_core->local_group_head;
                curr_core->local_group_head = new_local_grp;
                new_local_grp->next = curr_head;


                points_to_assign -= local_points_to_assign;
                if (points_to_assign == 0) {
                    break;
                }
            }
        }

        printf("getting next\n");
        curr_group = curr_group->next;
    }

    printf("\n");

}


struct group *get_group(int group_id) {
    struct group *curr_group = gs->group_head;
    while (curr_group) {
        if (curr_group->group_id == group_id) {
            return curr_group;
        }
        curr_group = curr_group->next;
    }
    return NULL;
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
    }
}

void re_enq_running_processes() {

    for (int i = 0; i < NUM_CORES; i++) {
        struct process *curr_process = gs->cores[i]->current_process;
        if (curr_process) {
            struct group *curr_group = get_group(curr_process->group_id);
            // add it to the end of the runqueue
            struct process *curr_head = curr_group->runqueue_head;
            while (curr_head->next) {
                curr_head = curr_head->next;
            }
            curr_head->next = curr_process;
            curr_process->next = NULL;
        }
    }
    
}


void schedule(int core_id, int time_passed) {

    // update timing
    struct process *running_process = gs->cores[core_id]->current_process;
    if (running_process) {
        struct local_group *curr_group = gs->cores[core_id]->local_group_head;
        while (curr_group) {
            if (curr_group->group_id == running_process->group_id) {
                curr_group->virt_time_gotten += (double)time_passed / curr_group->local_weight;
                break;
            }
            curr_group = curr_group->next;
        }
    }

    pthread_mutex_lock(&global_lock);
    // re-enq the running process
    if (running_process) {
        struct group *curr_group = get_group(running_process->group_id);
        // add it to the end of the runqueue
        struct process *curr_head = curr_group->runqueue_head;
        while (curr_head->next) {
            curr_head = curr_head->next;
        }
        curr_head->next = running_process;
        running_process->next = NULL;
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

    struct group *global_group_to_run = get_group(min_local_group->group_id);
    struct process *next_p = global_group_to_run->runqueue_head;
    global_group_to_run->runqueue_head = next_p->next;
    next_p->next = NULL;

    gs->cores[core_id]->current_process = next_p;

    pthread_mutex_unlock(&global_lock);

}



// add to group
void enqueue(struct process *p) {


    pthread_mutex_lock(&global_lock);
    
    free_all_local_groups();
    re_enq_running_processes();

    // add the new process to its group
    struct group *p_group = get_group(p->group_id);
    struct process *curr_head = p_group->runqueue_head;
    p_group->runqueue_head = p;
    p->next = curr_head;
    p_group->num_threads += 1;

    distribute_weight();

    pthread_mutex_unlock(&global_lock);

}

void dequeue(struct process *p) {

    pthread_mutex_lock(&global_lock);
    
    free_all_local_groups();
    re_enq_running_processes();

    // remove the process from its group
    struct group *p_group = get_group(p->group_id);
    struct process *curr_p = p_group->runqueue_head;
    while (curr_p->next) {
        if (curr_p->next->process_id == p->process_id) {
            curr_p->next = curr_p->next->next;
            break;
        }
        curr_p = curr_p->next;
    }
    curr_p->next = NULL; // it's the last one
    p_group->num_threads -= 1;

    distribute_weight();

    pthread_mutex_unlock(&global_lock);

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
            printf("  local group %d, weight %f\n", curr_local_group->group_id, curr_local_group->local_weight);
            curr_local_group = curr_local_group->next;
        }
    }

}


void main() {

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
            struct process *p = create_process(i*num_threads_p_group+j, i);
            enqueue(p);
        }
    }


    // schedule();
    print_global_state();

}







// when do core assignments change? 
// - positive (ie new work that needs to potentially take over from existing work)
//      - when underserved groups get a new thread
//      - when a new group starts
// - negative (ienew space is created where existing work can be moved to)
//      - when overserved group loses enough threads to become underserved
//      - when a group stops/is removed (is that really different from the first?)

