#include <pthread.h>
#include <stdint.h> 

#define DUMMY  -1

#include "vt.h"
#include "heap.h"

struct process {
	int process_id;
	struct group *group;
	int core_id;
	struct process *next;
} __attribute__((aligned(64)));

struct group {
	int group_id;
	int weight;

	pthread_rwlock_t group_lock;

	int num_threads; // the total number of threads in the system
	int threads_queued; // the number of threads runnable and in the q (ie not running)
	int nrunning;
	vt_t vruntime; // updated when the group is scheduled, assuming full tick

	t_t runtime;  // number of us the group ran
	t_t *sleeptime; // number of us slots the group wasn't runnable
	t_t *sleepstart; // tick slots sleep started
	t_t *time;
	
	struct process *runqueue_head;
	struct group *next;
	struct heap_elem heap_elem;
	struct mheap *mh;
	struct lock_heap *lh;
} __attribute__((aligned(64)));

struct group *grp_new(struct mheap *mh, int id, int weight);
void grp_print(struct group *g);
bool grp_dummy(struct group *g);
struct process *grp_new_process(int id, struct group *g);
int grp_cmp(void *e0, void *e1);
vt_t grp_get_vruntime(struct group *g);
void grp_upd_vruntime(struct group *g, t_t tick_length);
void grp_set_init_vruntime(struct group *g, vt_t min);
void grp_lag_vruntime(struct group *g, vt_t min);
bool grp_adjust_vruntime(struct group *g, t_t time_passed, t_t tick_length);
void grp_add_process(struct process *p);
struct process *grp_deq_process(struct group *g);
void grp_enqueue(struct group *g);
bool grp_is_sleep(struct group *g);



