#include <pthread.h>
#include <stdint.h> 
#include <stdbool.h>

#define DUMMY  -1

#include "vt.h"
#include "heap.h"

#ifndef GROUP_H
#define GROUP_H

#define SHARD_MAX_WEIGHT 10 // somewhat random

struct process {
	int process_id;
	struct group *group;
	struct group_shard *group_shard;
	int core_id;
	struct process *next;
} __attribute__((aligned(64)));

struct group {
	int group_id;
	int weight;
	struct group_shard *shard_head;
	struct group *next;

	// is this lock needed? only used to update the time accounting fields at the bottom
	pthread_rwlock_t group_lock; // LOCK ORDER: group_lock -> shard_lock

	int nthread; // number of threads in the group
	int nqueued; // number of threads runnable
	int nrunning; // number of threads running

	t_t runtime;  // number of us the group ran
	t_t *sleeptime; // number of us slots the group wasn't runnable
	t_t *sleepstart; // tick slots sleep started
	t_t *time;
} __attribute__((aligned(64)));

struct group_shard {
	int shard_id;
	int weight;
	struct group *group;

	pthread_rwlock_t shard_lock; // LOCK ORDER: group_lock -> shard_lock

	int nthread; // number of threads in the shard
	int nqueued; // number of threads runnable
	int nrunning; // number of threads running
	vt_t vruntime; // updated when the group is scheduled, assuming full tick
	
	struct process *runqueue_head;
	struct group_shard *next;
	struct heap_elem heap_elem;
	struct mheap *mh;
	struct lock_heap *lh;
} __attribute__((aligned(64)));

struct group *grp_new(struct mheap *mh, int id, int weight);
void grp_print(struct group *g);
void grp_shard_print(struct group_shard *s);
bool grp_shard_dummy(struct group_shard*s);
struct process *grp_new_process(int id, struct group *g);
int grp_shard_cmp(void *e0, void *e1);
vt_t grp_shard_get_vruntime(struct group_shard *s);
void grp_shard_upd_vruntime(struct group_shard *s, t_t tick_length);
void grp_shard_set_init_vruntime(struct group_shard *s, vt_t min);
void grp_shard_lag_vruntime(struct group_shard *s, vt_t min);
bool grp_shard_adjust_vruntime(struct group_shard *s, t_t time_passed, t_t tick_length);
struct group_shard *grp_pick_shard(struct group *g);
void grp_add_processL(struct process *p);
struct process *grp_shard_deq_process(struct group_shard *s);
void grp_shard_enqueue(struct group_shard *s);
bool grp_shard_is_sleep(struct group_shard *s);


#endif
