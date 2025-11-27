#ifndef _GRP_H_

#define _GRP_H_

#include <pthread.h>
#include <stdint.h> 

#include "util.h"
#include "vt.h"
#include "heap_elem.h"
#include "heap.h"

struct process {
	struct heap_elem he;
	struct heap *h;

	t_t runtime;  // __calign__;  // number of us the process ran

	struct mheap *mh;
	struct group *group;

	int pid;
	struct process *next;

	int other_hid;
	vt_t other_vt;

	int cid __calign__;     // core that is running or ran last this process
	
} __calign__;

struct group {
	vt_t vruntime  __calign__;

	vt_t min_vt_deq __calign__;
	int nthread; // number of threads in the group

	t_t *sleeptime; // number of us slots the group wasn't runnable
	t_t *sleepstart; // tick slots sleep started
	t_t *time;

	struct process *procs __calign__;
	struct mheap *mh;
	int gid;
	int weight;
} __calign__;

struct group *grp_new(struct mheap *mh, int id, int weight);
void proc_print(struct process *p);
struct process *grp_new_process(struct mheap *mh, int id, struct group *g);
int proc_cmp(struct heap_elem *e0, struct heap_elem *e1);

void grp_stats(struct group *g, long tot);
void grp_print(struct group *g);
void grp_set_vruntime(struct process *p, vt_t min);
vt_t grp_add_vruntime(struct process *p, vt_t min);

#endif



