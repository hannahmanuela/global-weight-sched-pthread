#include <pthread.h>
#include <stdint.h> 

#define DUMMY  -1

#include "vt.h"
#include "heap.h"

struct process {
	int pid;
	int weight;

	vt_t vruntime; 

	pthread_rwlock_t proc_lock;

	struct group *group;
	struct process *next;

	struct heap_elem heap_elem;
	struct mheap *mh;
	struct lock_heap *lh;
} __attribute__((aligned(64)));

struct group {
	int gid;
	int weight;

	pthread_rwlock_t group_lock;

	int nthread; // number of threads in the group
	int nqueued; // number of threads runnable

	t_t runtime;  // number of us the group ran
	t_t *sleeptime; // number of us slots the group wasn't runnable
	t_t *sleepstart; // tick slots sleep started
	t_t *time;
	
	struct process *runqueue_head;
	struct mheap *mh;
} __attribute__((aligned(64)));

struct group *grp_new(struct mheap *mh, int id, int weight);
vt_t grp_slot(struct process *p, int nthread);
void proc_print(struct process *p);
bool proc_dummy(struct process *p);
struct process *grp_new_process(struct mheap *mh, int id, struct group *g);
int proc_cmp(void *e0, void *e1);
vt_t proc_get_vruntime(struct process *p);
void proc_add_vruntime(struct process *p, t_t tick_length);
void proc_set_init_vruntime(struct process *p, vt_t min);
void proc_lag_vruntime(struct process *p, vt_t min);
bool proc_adjust_vruntime(struct process *p, t_t time_passed, t_t tick_length);

void grp_add_process(struct process *p);
void grp_enqueue(struct group *g);
void grp_stats(struct group *g, long tot);



