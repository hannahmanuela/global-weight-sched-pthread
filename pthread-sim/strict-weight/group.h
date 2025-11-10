#ifndef _GRP_H_

#define _GRP_H_

#include <pthread.h>
#include <stdint.h> 

#include "vt.h"
#include "heap_elem.h"
#include "heap.h"

struct process {
	struct heap_elem he;
	struct lheap *lh;
	t_t runtime;  // number of us the process ran
	pthread_rwlock_t proc_lock;

	struct mheap *mh;
	struct group *group;

	int pid;
	struct process *next;
} __attribute__((aligned(64)));

struct group {
	int nthread; // number of threads in the group
	int nqueued; // number of threads runnable

	int gid;
	int weight;

	pthread_rwlock_t group_lock;

	t_t *sleeptime; // number of us slots the group wasn't runnable
	t_t *sleepstart; // tick slots sleep started
	t_t *time;
	
	struct process *procs;
	struct mheap *mh;
} __attribute__((aligned(64)));

struct group *grp_new(struct mheap *mh, int id, int weight);
vt_t grp_slot(struct process *p, int nthread);
void proc_print(struct process *p);
struct process *grp_new_process(struct mheap *mh, int id, struct group *g);
int proc_cmp(struct heap_elem *e0, struct heap_elem *e1);
vt_t proc_get_vruntime(struct process *p);
void proc_add_vruntime(struct process *p, vt_t tick_length);
void proc_set_init_vruntime(struct process *p, vt_t min);
void proc_lag_vruntime(struct process *p, vt_t min);
bool proc_adjust_vruntime(struct process *p, t_t time_passed, t_t tick_length);
void proc_insert_mh(struct process *p, struct lheap *lh);

void grp_add_process(struct process *p);
void grp_enqueue(struct group *g);
void grp_stats(struct group *g, long tot);
float grp_runtime(struct group *g);

#endif



