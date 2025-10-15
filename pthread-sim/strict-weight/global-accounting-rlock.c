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
// #define ASSERTS
// #define ASSERTS_SINGLE_WORKER

// #define TIME_TO_RUN 10000000LL
#define TIME_TO_RUN 1000000LL

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
    int heap_index; // index within the global group's min-heap, -1 if not present
} __attribute__((aligned(64)));

struct core_state {
	int core_id;
	struct process *current_process;
	long sched_cycles;
	long enq_cycles;
	long yield_cycles;
	int nsched;
	int nenq;
	int nyield;
} __attribute__((aligned(64)));

struct group_list {
	pthread_rwlock_t group_list_lock;
    // Min-heap of groups keyed by spec_virt_time
    struct group **heap;
    int heap_size;
    int heap_capacity;

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
    g->threads_queued = 0;
    g->spec_virt_time = 0;
    g->virt_lag = 0;
    g->last_virt_time = 0;
    g->runqueue_head = NULL;
    g->next = NULL;
    g->heap_index = -1;
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
	printf("%ld cycles: sched %ld(%0.2f) enq %ld(%0.2f) yield %ld (%0.2f)\n",
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

void print_global_state() {
    pthread_rwlock_rdlock(&gs->glist->group_list_lock);
    printf("Global heap size: %d (empty: %d)\n", gs->glist->heap_size, num_groups_empty);
    printf("Heap contents by (group_id: svt, num_threads, num_queued): ");
    for (int i = 0; i < gs->glist->heap_size; i++) {
        struct group *g = gs->glist->heap[i];
        pthread_rwlock_rdlock(&g->group_lock);
        printf("(%d: %d, %d, %d)%s", g->group_id, g->spec_virt_time, g->num_threads, g->threads_queued, i == gs->glist->heap_size - 1 ? "\n" : ", ");
        pthread_rwlock_unlock(&g->group_lock);
    }
    pthread_rwlock_unlock(&gs->glist->group_list_lock);
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
    int num_groups_found = gs->glist->heap_size;
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
    
    pthread_rwlock_rdlock(&gs->glist->group_list_lock);
    for (int i = 0; i < gs->glist->heap_size; i++) {
        if (gs->glist->heap[i] == g) {
            pthread_rwlock_unlock(&gs->glist->group_list_lock);
            assert(0);
        }
    }
    pthread_rwlock_unlock(&gs->glist->group_list_lock);
}

void __assert_group_in_list(struct group *g) {
    assert(num_cores == 1);
    pthread_rwlock_rdlock(&gs->glist->group_list_lock);
    for (int i = 0; i < gs->glist->heap_size; i++) {
        if (gs->glist->heap[i] == g) {
            pthread_rwlock_unlock(&gs->glist->group_list_lock);
            return;
        }
    }
    pthread_rwlock_unlock(&gs->glist->group_list_lock);
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

// -----------------
// heap helpers
// -----------------
static inline int gl_cmp_group(struct group *a, struct group *b) {
    // Compare by spec_virt_time; lower is higher priority
    if (a->spec_virt_time < b->spec_virt_time) return -1;
    if (a->spec_virt_time > b->spec_virt_time) return 1;
    // tie-breaker by group_id for determinism
    if (a->group_id < b->group_id) return -1;
    if (a->group_id > b->group_id) return 1;
    return 0;
}

static inline void gl_heap_swap(struct group_list *gl, int i, int j) {
    struct group *tmp = gl->heap[i];
    gl->heap[i] = gl->heap[j];
    gl->heap[j] = tmp;
    gl->heap[i]->heap_index = i;
    gl->heap[j]->heap_index = j;
}

static void gl_heap_sift_up(struct group_list *gl, int idx) {
    while (idx > 0) {
        int parent = (idx - 1) / 2;
        if (gl_cmp_group(gl->heap[idx], gl->heap[parent]) < 0) {
            gl_heap_swap(gl, idx, parent);
            idx = parent;
        } else {
            break;
        }
    }
}

static void gl_heap_sift_down(struct group_list *gl, int idx) {
    int n = gl->heap_size;
    while (1) {
        int left = idx * 2 + 1;
        int right = idx * 2 + 2;
        int smallest = idx;
        if (left < n && gl_cmp_group(gl->heap[left], gl->heap[smallest]) < 0) {
            smallest = left;
        }
        if (right < n && gl_cmp_group(gl->heap[right], gl->heap[smallest]) < 0) {
            smallest = right;
        }
        if (smallest != idx) {
            gl_heap_swap(gl, idx, smallest);
            idx = smallest;
        } else {
            break;
        }
    }
}

static void gl_heap_ensure_capacity(struct group_list *gl) {
    if (gl->heap_size < gl->heap_capacity) return;
    int new_capacity = gl->heap_capacity == 0 ? 16 : gl->heap_capacity * 2;
    gl->heap = realloc(gl->heap, sizeof(struct group*) * new_capacity);
    gl->heap_capacity = new_capacity;
}

// reheapify a group at its current index after its key changed; caller must hold group_list_lock
static inline void gl_heap_fix_index(struct group_list *gl, int idx) {
    if (idx < 0 || idx >= gl->heap_size) return;
    gl_heap_sift_down(gl, idx);
    gl_heap_sift_up(gl, idx);
}

// peek min group without removing; returns with no locks held
static struct group* gl_peek_min_group(struct group_list *gl) {
    pthread_rwlock_rdlock_fail(&gl->group_list_lock, &gl->nfail_min);
    struct group *g = gl->heap_size > 0 ? gl->heap[0] : NULL;
    pthread_rwlock_unlock(&gl->group_list_lock);
    return g;
}

// push group into heap; caller must hold group_list_lock
static inline void gl_heap_push(struct group_list *gl, struct group *g) {
    gl_heap_ensure_capacity(gl);
    g->heap_index = gl->heap_size;
    gl->heap[gl->heap_size++] = g;
    gl_heap_sift_up(gl, g->heap_index);
}

// remove group at index; caller must hold group_list_lock
static inline void gl_heap_remove_at(struct group_list *gl, int idx) {
    int last = gl->heap_size - 1;
    if (idx < 0 || idx >= gl->heap_size) return;
    if (idx != last) {
        gl_heap_swap(gl, idx, last);
    }
    struct group *removed = gl->heap[last];
    gl->heap_size--;
    removed->heap_index = -1;
    if (idx < gl->heap_size) {
        gl_heap_sift_down(gl, idx);
        gl_heap_sift_up(gl, idx);
    }
}

// compute avg_spec_virt_time for groups in gl, ignoring group_to_ignore
// caller should have no locks
int gl_avg_spec_virt_time(struct group_list *gl, struct group *group_to_ignore) {
    int total_spec_virt_time = 0;
    int count = 0;
    pthread_rwlock_rdlock_fail(&gl->group_list_lock, &gl->nfail_time);
    for (int i = 0; i < gl->heap_size; i++) {
        struct group *g = gl->heap[i];
        if (g == group_to_ignore) continue;
        total_spec_virt_time += grp_get_spec_virt_time(g);
        count++;
    }
    pthread_rwlock_unlock(&gl->group_list_lock);
    if (count == 0) return 0;
    return total_spec_virt_time / count;
}


void gl_add_group(struct group_list *gl, struct group *g) {
    pthread_rwlock_wrlock_fail(&gl->group_list_lock, &gl->nfail_list);
    if (g->heap_index == -1) {
        gl_heap_push(gl, g);
        num_groups_empty--;
    }
    pthread_rwlock_unlock(&gl->group_list_lock);
}

void gl_del_group(struct group_list *gl, struct group *g) {
    pthread_rwlock_wrlock_fail(&gl->group_list_lock, &gl->nfail_list);
    if (g->heap_index != -1) {
        gl_heap_remove_at(gl, g->heap_index);
        num_groups_empty++;
    }
    pthread_rwlock_unlock(&gl->group_list_lock);
}

// Update group's spec_virt_time and safely reheapify if the group is in the heap.
// Caller is allowed to hold the group lock on entry; to respect the lock invariant
// (never take the global list lock while holding a group lock), this function may
// temporarily release and then reacquire the group lock if reheapification is needed.
void gl_update_group_svt(struct group_list *gl, struct group *g, int diff) {
    int initial_heap_index;
    // We might be called with the group lock held; do the update and capture heap index
    g->spec_virt_time += diff;
    initial_heap_index = g->heap_index;

    // If not in heap, nothing else to do
    if (initial_heap_index == -1) {
        return;
    }

    // Respect lock invariant: release group lock before taking the global list lock
    pthread_rwlock_unlock(&g->group_lock);
    pthread_rwlock_wrlock_fail(&gl->group_list_lock, &gl->nfail_list);

    // The group's index may have changed; read the current index under list lock
    int current_index = g->heap_index;
    if (current_index != -1) {
        gl_heap_fix_index(gl, current_index);
    }
    pthread_rwlock_unlock(&gl->group_list_lock);

    // Reacquire group lock before returning to preserve caller's expectation
    pthread_rwlock_wrlock(&g->group_lock);
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
void grp_collapse_spec_virt_time(struct group_list *gl, struct group *g, int time_passed) {
	int p_grp_weight = grp_get_weight(g);
    int time_had_expected = (int) (tick_length / p_grp_weight);
    
    // update spec virt time if time gotten was not what this core expected
    int virt_time_gotten = (int)(time_passed / p_grp_weight);
    if (time_had_expected  != virt_time_gotten) {
        // need to edit the spec time to use time actually got
        int diff = virt_time_gotten - time_had_expected;

        pthread_rwlock_wrlock(&g->group_lock);
        g->spec_virt_time += diff;
        int idx = g->heap_index;
        pthread_rwlock_unlock(&g->group_lock);

        // If the group is currently in the heap, fix its position
        if (idx != -1) {
            pthread_rwlock_wrlock_fail(&gl->group_list_lock, &gl->nfail_list);
            gl_heap_fix_index(gl, idx);
            pthread_rwlock_unlock(&gl->group_list_lock);
        }
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

    if (!now_empty) {
        return;
    }

    // there's a potential race with enq here
    // removing the group is ok b/c enq will have added a second copy of the group for a bit
    // but setting the virt time is not ok, need to re-check
    gl_del_group(gl, p->group);

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
        grp_collapse_spec_virt_time(gl, running_process->group, time_passed);
    }

    if (should_re_enq && running_process) {
	    enqueue(gl, running_process, 0);
    }

    core->current_process = NULL;

    struct group *min_group = gl_peek_min_group(gl);
    if (min_group == NULL)
	    return;

    // select the next process
    pthread_rwlock_wrlock(&min_group->group_lock);
    int time_expecting = (int)tick_length / min_group->weight;
    gl_update_group_svt(gl, min_group, time_expecting);

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
        // print_global_state();
	    mycore->sched_cycles += safe_read_tsc() - ts;
	    mycore->nsched += 1;

        if (mycore->current_process) {
            assert_thread_counts_correct(mycore->current_process->group, mycore);
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
            // print_global_state();
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

            // print_global_state();

            assert_p_not_in_group(p, p->group);
		    trace_yield(p->process_id, p->group->group_id, core_id);
		    p->next = pool;
		    pool = p;
		    usleep((int)(tick_length / 2));
	    }
        
    }
}


#ifndef UNIT_TEST
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
    gs->glist->heap = NULL;
    gs->glist->heap_size = 0;
    gs->glist->heap_capacity = 0;
    gs->glist->nfail_min = 0;
    gs->glist->nfail_time = 0;
    gs->glist->nfail_list = 0;
    gs->cores = (struct core_state *) malloc(sizeof(struct core_state)*num_cores);
    pthread_rwlock_init(&gs->glist->group_list_lock, NULL);
    for (int i = 0; i < num_cores; i++) {
        gs->cores[i].core_id = i;
        gs->cores[i].current_process = NULL;
        gs->cores[i].sched_cycles = 0;
        gs->cores[i].enq_cycles = 0;
        gs->cores[i].yield_cycles = 0;
        gs->cores[i].nsched = 0;
        gs->cores[i].nenq = 0;
        gs->cores[i].nyield = 0;
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
#endif // UNIT_TEST





