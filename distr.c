#define _GNU_SOURCE
#include <sys/time.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sched.h>

// the global data structures that we need access to

struct global_state* gs;

#define NUM_CORES 56

struct group_node {
    int group_id;
    int weight;
    int core_taken;
    struct group_node *next;
}

struct group_rq {
    int group_id;
    struct process *head;
    struct group_rq *next;
}

struct process {
    int process_id;
    int group_id;
    struct process *next;
}

struct core_state {
    int core_id;
    struct process *current_process;
}

struct groups_cores_waiting {
    int group_id;
    int core_ids[NUM_CORES]; // each core would put in the weight the group has locally, if it's waiting
    struct groups_cores_waiting *next;
}

struct global_state {
    int weight_per_core;
    struct group_node *group_node_head;
    struct group_rq *group_rq_head;
    struct groups_cores_waiting *groups_cores_waiting_head;
    struct core_state *cores[NUM_CORES];
}

struct process *create_process(int id, int group_id, struct process *next) {
    struct process *p = malloc(sizeof(struct process));
    p->process_id = id;
    p->group_id = group_id;
    p->next = next;
    return p;
}

struct group_node create_group_node(int group_id, int weight) {
    struct group_node *g = malloc(sizeof(struct group_node));
    g->group_id = group_id;
    g->weight = weight; // for now we are giving it the full weight
    g->core_taken = 0;
    g->next = NULL;
    return g;
}

struct group_rq *create_group_rq(int group_id) {
    struct group_rq *g = malloc(sizeof(struct group_rq));
    g->group_id = group_id;
    g->head = NULL;
    g->next = NULL;
    return g;
}

struct group_rq *get_group_rq(int group_id) {
    struct group_rq *curr_group_rq = gs->group_rq_head;
    while (curr_group_rq) {
        if (curr_group_rq->group_id == group_id) {
            return curr_group_rq;
        }
    }
    return NULL;
}


void print_global_state() {
    printf("global state:\n");
    printf("total weight: %d\n", gs->total_weight);
    struct group_node *curr_group = gs->group_node_head;
    while (curr_group) {
        printf("node: group id %d, weight %d, core taken %d\n", curr_group->group_id, curr_group->weight, curr_group->core_taken);
        curr_group = curr_group->next;
    }
    struct group_rq *curr_group_rq = gs->group_rq_head;
    while (curr_group_rq) {
        printf("rq: group id %d\n", curr_group_rq->group_id);
        struct process *p = curr_group_rq->head;
        while (p) {
            printf("  process %d\n", p->process_id);
            p = p->next;
        }
        curr_group_rq = curr_group_rq->next;
    }

    printf("cores:\n");
    for (int i = 0; i < NUM_CORES; i++) {
        printf("core %d: %d\n", i, gs->cores[i]->current_process ? gs->cores[i]->current_process->process_id : -1);
    }
}


// LOCAL data structures

struct local_group_state {
    int group_id;
    long long time_gotten;
    int local_weight;
    struct local_group_state *next;
}

struct local_core_state {
    struct local_group_state *head;
}

void create_groups_cores_waiting(int group_id, int core_id, int local_weight) {
    struct groups_cores_waiting *curr_groups_cores_waiting = gs->groups_cores_waiting_head;
    while (curr_groups_cores_waiting) {
        if (curr_groups_cores_waiting->group_id == group_id) {
            curr_groups_cores_waiting->core_ids[core_id] = local_weight;
            break;
        }
        curr_groups_cores_waiting = curr_groups_cores_waiting->next;
    }
}


void schedule(int core_id, int time_passed, int still_running, local_core_state *local_core_state) {

    struct process *running_process = gs->cores[core_id]->current_process;
    struct process *to_run_next = NULL;

    // update timing locally
    if (running_process) {
        struct local_group_state *curr_group = local_core_state->head;
        while (curr_group) {
            if (curr_group->group_id == running_process->group_id) {
                curr_group->time_gotten += time_passed;
                break;
            }
            curr_group = curr_group->next;
        }
    }


    // re-enq the old current
    if (running_process && still_running) {
        gs->cores[core_id]->current_process = NULL;
        enqueue(running_process);
    } else if (running_process) {
        gs->cores[core_id]->current_process = NULL;
        // TODO: what if we are hereby dequeueing the last thread in the group? 
        // we could create some sort of implicit dequeue...
    }


    // pick the next process

    // for every group, calculate number of cores that it should get based on its weight
    int sum_time_gotten = 0;
    int num_groups = 0;
    struct local_group_state *curr_group = local_core_state->head;
    while (curr_group) {
        sum_time_gotten += curr_group->time_gotten * curr_group->local_weight;
        num_groups += 1;
        curr_group = curr_group->next;
    }
    int avg_time_gotten_p_weight = (int)(sum_time_gotten / num_groups);

    // find the group with the least lag
    int min_lag = INT_MAX;
    struct local_group_state *min_group = NULL;
    struct local_group_state *curr_group = local_core_state->head;
    while (curr_group) {
        int lag = (curr_group->time_gotten * curr_group->local_weight) - avg_time_gotten_p_weight;
        if (lag < min_lag) {
            min_lag = lag;
            min_group = curr_group;
        }
        curr_group = curr_group->next;
    }
    int group_want = min_group->group_id;
    struct group_rq *p_group_rq = get_group_rq(group_want);

    // attempt to get a process from the chosen group
again:
    if (!p_group_rq->head) {
        // register that the core is waiting for a process in this group
        create_groups_cores_waiting(group_want, core_id, min_group->local_weight);
        // move on to running a different group
        goto smth_else;
    } else {
        struct process *curr_head_p = p_group_rq->head;
        struct process *curr_next_process = curr_head_p->next;
        int old_actual_value = __sync_val_compare_and_swap(&p_group_rq->head, curr_head_p, curr_next_process);
        if (old_actual_value != curr_head_p) {
            goto again;
        } else {
            to_run_next = curr_head_p;
            goto found;
        }
    }
    
smth_else:
    // run a different group
    // try to get a process from any other group
    struct group_rq *curr_group = local_core_state->head;
    while (curr_group) {
        struct group_rq *curr_group_rq = get_group_rq(curr_group->group_id);

    again2:
        if (curr_group_rq->head) {
            struct process *curr_head = curr_group_rq->head;
            struct process *curr_next_process = curr_head->next;
            int old_actual_value = __sync_val_compare_and_swap(&curr_group_rq->head, curr_head, curr_next_process);
            if (old_actual_value != curr_head) {
                goto again2;
            } else {
                to_run_next = curr_head;
                goto found;
            }
        }
        curr_group = curr_group->next;
    }

found:
    gs->cores[core_id]->current_process = to_run_next;
    return;
}

// removes a p from the global rq -- do we actually need this right now?
// already have a way of dequeueing currently running process by just not re-enqing it
void dequeue(struct process *p) {

    // what if it's the very last thread in the group (if the weight waiting for it is the groups full weight?)
    int is_last_thread = 0;

    int p_group_weight = 0;
    struct group_node *curr_group_node = gs->group_node_head;
    while (curr_group_node) {
        if (curr_group_node->group_id == p->group_id) {
            p_group_weight += curr_group_node->weight;
        }
        curr_group_node = curr_group_node->next;
    }

    struct groups_cores_waiting *curr_groups_cores_waiting = gs->groups_cores_waiting_head;
    while (curr_groups_cores_waiting) {
        if (curr_groups_cores_waiting->group_id == p->group_id) {
            if (curr_groups_cores_waiting->core_ids[core_id] == p_group_weight) {
                is_last_thread = 1;
                break;
            }
        }
        curr_groups_cores_waiting = curr_groups_cores_waiting->next;
    }

    // TODO: what if it is the last thread? -- do we actually need to do this on thread exit? what if we did it on actual group removal?
    if (is_last_thread) {
        // remove all the nodes from the node rq
        // remove all the cores waiting for this group (and notify them?)
        // remove the group rq from the global rq
        // recalculate weight per core
        // edit the node list to ensure none of the nodes have more weight than the new weight per core
    }

    // otherwise, just remove it
deq_again:
    struct group_rq *p_group_rq = get_group_rq(p->group_id);
    struct process *curr_p = p_group_rq->head;
    struct process *prev_p = NULL;
    if (curr_p == p) {
        int old_actual_value = __sync_val_compare_and_swap(&p_group_rq->head, curr_p, curr_p->next);
        if (old_actual_value != curr_p) {
            goto deq_again;
        } else {
            goto done;
        }
    }
    while (curr_p->next->next) {
        prev_p = curr_p;
        curr_p = curr_p->next;
        if (curr_p == p) {
            int old_actual_value = __sync_val_compare_and_swap(&prev_p->next, curr_p, curr_p->next);
            if (old_actual_value != curr_p) {
                goto deq_again;
            } else {
                goto done;
            }
        }
    }
    curr_p = curr_p->next;
    if (curr_p == p) {
        int old_actual_value = __sync_val_compare_and_swap(&curr_p->next, p, NULL);
        if (old_actual_value != p) {
            goto deq_again;
        } else {
            goto done;
        }
    }
    printf("deq error this should be unreachable: process %d not found in group %d\n", p->process_id, p->group_id);

done:
    p->next = NULL;
    return;
}

// adds a new p to the global rq
void enqueue(struct process *p) {

    // what if it's the first thread in the group to exist
    int found = 0;
    struct group_node *curr_group_node = gs->group_node_head;
    while (curr_group_node) {
        if (curr_group_node->group_id == p->group_id) {
            found = 1;
            break;
        }
        curr_group_node = curr_group_node->next;
    }
    if (!found) {
        // TODO: need some sort of lock here?
        // need to change all the everythings
    }

    // what if p is a thread that some core is waiting for 
    // TODO: (or we have a core that is idle? does that exist outside of waiting for a group once we start running anything?)
    int some_core_is_waiting = 0;
    struct groups_cores_waiting *curr_groups_cores_waiting = gs->groups_cores_waiting_head;
    while (curr_groups_cores_waiting) {
        if (curr_groups_cores_waiting->group_id == p->group_id) {
            some_core_is_waiting = 1;
            break;
        }
        curr_groups_cores_waiting = curr_groups_cores_waiting->next;
    }
    if (some_core_is_waiting) {
        // TODO: need some way of sending IPI to the core that is waiting
    }

    // default case: just add it to the runqueue
    struct group_rq *p_group_rq = get_group_rq(p->group_id);
enq_again:
    // there should be a head, if there's not then we need to start again? they may have registered themselves in the meantime
    // there should be a bound to how much feathering there can be, maybe wrap in a lock? (given that this is a rarer expected event)
    struct process *curr_p = p_group_rq->head;
    while (curr_p->next) {
        curr_p = curr_p->next;
    }
    int old_actual_value = __sync_val_compare_and_swap(&curr_p->next, NULL, p);
    if (old_actual_value != NULL) {
        // someone else got it
        goto enq_again;
    }

}


void run_core(void* core_num_ptr) {

    int core_id = (int)core_num_ptr;

    struct process *pool = NULL;

    struct local_core_state *local_core_state = malloc(sizeof(struct local_core_state));
    local_core_state->head = NULL;

    // pin to an actual core
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

    // TODO: what do idle cores do? how do they wait for something to be enqueued? (need to set up IPIs)

    while (1) {

        // time how long it takes to schedule, write to file
        struct timeval start, end;
        gettimeofday(&start, NULL);

        pthread_mutex_lock(&global_lock);
        schedule(core_id, 4000, 0, local_core_state);
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
        } else {
            usleep(4000); // just run
        }
        // if (choice == 1) {
        //     // pick an exisitng process from the pool?
        //     struct process *p = pool;
        //     if (!p) {
        //         continue;
        //     }
        //     pool = p->next;
        //     p->next = NULL;
        //     pthread_mutex_lock(&global_lock);
        //     enqueue(p);
        //     pthread_mutex_unlock(&global_lock);
        // } else {
        //     pthread_mutex_lock(&global_lock);
        //     struct process *p = gs->cores[core_id]->current_process;
        //     if (!p) {
        //         pthread_mutex_unlock(&global_lock);
        //         continue;
        //     }
        //     dequeue(p);
        //     pthread_mutex_unlock(&global_lock);
        //     p->next = pool;
        //     pool = p;
        // }
        
    }


}


void main() {
    pthread_mutex_init(&global_lock, NULL);

    // empty file
    FILE *f = fopen("schedule_time.txt", "w");
    fclose(f);

    gs = malloc(sizeof(struct global_state));
    gs->weight_per_core = 0;
    gs->group_node_head = NULL;
    gs->group_rq_head = NULL;
    gs->groups_cores_waiting_head = NULL;
    for (int i = 0; i < NUM_CORES; i++) {
        gs->cores[i] = malloc(sizeof(struct core_state));
        gs->cores[i]->core_id = i;
        gs->cores[i]->current_process = NULL;
    }


    // create a couple groups and a bunch of processes
    int num_groups = 10;
    int num_processes = 10;
    struct group_rq *prev_group_rq = NULL;
    for (int i = 0; i < num_groups; i++) {
        struct group_rq *g = create_group_rq(i);
        if (!prev_group_rq) {
            gs->group_rq_head = g;
        } else {
            prev_group_rq->next = g;
        }
        prev_group_rq = g;
        for (int j = 0; j < num_processes; j++) {
            struct process *p = create_process(i*num_processes+j, i, NULL);
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



