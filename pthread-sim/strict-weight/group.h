#include <pthread.h>
#include <stdint.h> 

#include "heap.h"

struct process {
	int process_id;
	struct group *group;
	int core_id;
	int tot_weight;
	struct process *next;
} __attribute__((aligned(64)));

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

	long runtime;  // number of us the group ran
	long sleeptime; // number of us the group wasn't runnable
	long *sleepstart; // tick sleep started
	long *time;
	
	struct process *runqueue_head;
	struct group *next;
	struct heap_elem heap_elem;
	struct mheap *mh;
	struct lock_heap *lh;
} __attribute__((aligned(64)));


struct group *grp_new(int id, int weight);
struct process *grp_new_process(int id, struct group *group);
int grp_cmp(void *e0, void *e1);
void grp_upd_spec_virt_time_avg(struct group *g, int delta);
int grp_get_spec_virt_time(struct group *g);
void grp_set_init_spec_virt_time(struct group *g, int avg);
void grp_lag_spec_virt_time(struct process *p, int avg);
bool grp_adjust_spec_virt_time(struct process *p, int time_passed, int tick_length);
void grp_add_process(struct process *p);
struct process *grp_deq_process(struct group *g);



