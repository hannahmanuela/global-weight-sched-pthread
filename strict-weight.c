#include <stdio.h>
#include <math.h>
#include <stdlib.h>

#define NUM_CORES 56


struct process {
    int id;
    int group_id;
    struct process *next;
};

struct group {
    int id;
    int weight;
    int underserved;
    int num_threads;
    struct process *runqueue_head;
    struct group *next;
};

struct local_group {
    int group_id;
    double local_weight;
    struct local_group *next;
};

struct core_state {
    int core_id;
    double remaining_capacity; // as a fraction, ie between 0 and 1
    struct local_group *local_group_head;
};

struct global_state {
    int total_weight;
    struct group *group_head;
    int num_groups;
    struct core_state *cores[NUM_CORES];
};

struct global_state* gs;


struct group *create_group(int id, int weight, int num_threads) {
    struct group *g = malloc(sizeof(struct group));
    g->id = id;
    g->weight = weight;
    g->num_threads = num_threads;
    g->underserved = 1;
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
    struct group* groups_arr[gs->num_groups];
    struct group *curr_group = gs->group_head;
    for (int i = 0; i < gs->num_groups; i++) {
        groups_arr[i] = curr_group;
        curr_group = curr_group->next;
    }

    // sort (descending weight per thread)
    qsort(groups_arr, gs->num_groups, sizeof(struct group *), compare_groups_by_weight_p_thread);

    // convert back to linked list
    for (int i = 0; i < gs->num_groups; i++) {
        if (i == 0) {
            gs->group_head = groups_arr[i];
        } else {
            groups_arr[i-1]->next = groups_arr[i];
        }
    }
    groups_arr[gs->num_groups-1]->next = NULL;

}

void distribute_weight() {

    int weight_left = gs->total_weight;
    int cores_left = NUM_CORES;
    double curr_w_p_core, curr_w_p_thread;

    // TODO: instead of sorting, do the two loops based on underserved vs not, can keep track of the same metrics
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
            for (int i=curr_core_assigning; i < NUM_CORES; i++) {
                struct core_state *curr_core = gs->cores[i];
                if (curr_core->remaining_capacity == 1) {
                    curr_core->remaining_capacity == 0;
                        struct local_group *new_local_grp = create_local_group(curr_group->id, 1);
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
        printf("group %d, curr w p core %f, should get %f cores\n", curr_group->id, curr_w_p_core, cores_group_should_get);
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

                struct local_group *new_local_grp = create_local_group(curr_group->id, local_points_to_assign);
                struct local_group *curr_head = curr_core->local_group_head;
                curr_core->local_group_head = new_local_grp;
                new_local_grp->next = curr_head;


                points_to_assign -= local_points_to_assign;
                if (points_to_assign == 0) {
                    break;
                }
            }
        }

        curr_group = curr_group->next;
    }

}



void schedule() {

    // TODO: remove/free the existing local group info?
    // TODO: don't do a full redistribution, just pull from group that core should be running

    distribute_weight();

}



// add to group
void enqueue(struct process *p) {

    // need to reshuffle if the group is currently underserved
    if (p->group->underserved) {
        // TODO: full reshuffle? is there a better way to do this?
        // TODO: check if the groups is still underserved
    }


}

// is this deqing a curr? (just do that by running schedule and not re-enqing it?)
// if not, we might need to check all the cores to see who is still running it?
void dequeue(struct process *p) {

    // TODO: check if the group is now underserved


}




// when do core assignments change? 
// - positive (ie new work that needs to potentially take over from existing work)
//      - when underserved groups get a new thread
//      - when a new group starts
// - negative (ienew space is created where existing work can be moved to)
//      - when overserved group loses enough threads to become underserved
//      - when a group stops/is removed (is that really different from the first?)


void print_global_state() {
    printf("global state:\n");
    printf("total weight: %d\n", gs->total_weight);
    struct group *curr_group = gs->group_head;
    while (curr_group) {
        printf("group %d, weight %d, num threads %d\n", curr_group->id, curr_group->weight, curr_group->num_threads);
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
    gs->num_groups = num_groups;
    for (int i = 0; i < NUM_CORES; i++) {
        gs->cores[i] = malloc(sizeof(struct core_state));
        gs->cores[i]->core_id = i;
        gs->cores[i]->local_group_head = NULL;
        gs->cores[i]->remaining_capacity = 1;
    }

    struct group *prev_group = NULL;
    for (int i = 0; i < num_groups; i++) {
        struct group *g = create_group(i, 10, num_threads_p_group);
        if (!prev_group) {
            gs->group_head = g;
        } else {
            prev_group->next = g;
        }
        prev_group = g;
        gs->total_weight += g->weight;
    }


    schedule();
    print_global_state();

}






