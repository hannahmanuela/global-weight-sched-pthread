#ifndef _GRP_H_

#define _GRP_H_

#include <pthread.h>
#include <stdint.h> 

#include "util.h"
#include "vt.h"
#include "heap.h"
#include "process.h"

#define DEF_NUM_GROUPS 4

struct group {
	vt_t vruntime  __calign__;
	vt_t lag;

	vt_t min_vt_deq __calign__;
	int nthread; // number of threads in the group

	t_t *sleeptime; // number of us slots the group wasn't runnable
	t_t *sleepstart; // tick slots sleep started
	t_t *time;

	struct task_struct *procs __calign__;
	struct mheap *mh;
	int gid;
	int weight;
} __calign__;


void grp_stats(struct group *g, long tot);
void grp_print(struct group *g);
void grp_set_vruntime(struct task_struct *p, vt_t min);
vt_t grp_add_vruntime(struct task_struct *p, vt_t min);
vt_t grp_add_lag(struct task_struct *p, vt_t min);
vt_t grp_load_lag(struct task_struct *p);

#endif



