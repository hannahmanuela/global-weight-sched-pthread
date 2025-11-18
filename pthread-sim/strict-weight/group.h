#ifndef _GRP_H_

#define _GRP_H_

#include <pthread.h>
#include <stdint.h> 

#include "vt.h"
#include "heap_elem.h"
#include "heap.h"

struct process {
	struct heap_elem he;
	struct heap *h;
	t_t runtime;  // number of us the process ran

	pthread_rwlock_t proc_lock;

	struct mheap *mh;
	struct group *group;

	int pid;
	struct process *next;
} __attribute__((aligned(64)));

struct group {
	vt_t vruntime  __attribute__((aligned(CACHE_LINE_SZ)));

	vt_t min_vt_deq __attribute__((aligned(CACHE_LINE_SZ)));
	int nthread; // number of threads in the group

	t_t *sleeptime; // number of us slots the group wasn't runnable
	t_t *sleepstart; // tick slots sleep started
	t_t *time;


	pthread_rwlock_t group_lock __attribute__((aligned(CACHE_LINE_SZ)));

	struct process *procs __attribute__((aligned(CACHE_LINE_SZ)));
	struct mheap *mh;
	int gid;
	int weight;
} __attribute__((aligned(64)));

struct group *grp_new(struct mheap *mh, int id, int weight);
vt_t grp_slot(struct process *p, int nthread);
void proc_print(struct process *p);
struct process *grp_new_process(struct mheap *mh, int id, struct group *g);
int proc_cmp(struct heap_elem *e0, struct heap_elem *e1);

void grp_stats(struct group *g, long tot);
void grp_print(struct group *g);
float grp_runtime(struct group *g);
void grp_set_vruntime(struct process *p, vt_t min);
vt_t grp_add_vruntime(struct process *p, vt_t min);

#endif



