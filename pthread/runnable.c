#define _GNU_SOURCE

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
#include <limits.h>
#include <float.h>

#include "vt.h"
#include "driver.h"
#include "core.h"
#include "group.h"
#include "sched_state.h"
#include "heap.h"
#include "mheap.h"
#include "util.h"

//
// queue of runnable processes using mheap
//

extern struct sched_state *ss_global;

struct task_struct *runnable_deq_proc_hint(struct mheap *mh, struct task_struct *prev, int hint) {
	struct heap_elem *he = mh_deq_min_elem_enq(mh, &prev->he, hint, ss_global->is_lt_elem);
	struct task_struct *p = container_of(he, struct task_struct, he);
	return p;
}

struct task_struct *runnable_deq_proc(struct mheap *mh, struct task_struct *prev) {
	return runnable_deq_proc_hint(mh, prev, -1);
}

struct task_struct *runnable_deq_proc_all_heap(struct mheap *mh) {
	struct heap_elem *he = mh_deq_min_elem_all_heap(mh, ss_global->is_min_elem);
	struct task_struct *p = container_of(he, struct task_struct, he);
	return p;
}

struct task_struct *runnable_deq_high_proc_all_heap(struct mheap *mh) {
	struct heap_elem *he = mh_deq_min_elem_all_heap(mh, ss_global->is_min_elem);
	struct task_struct *p = container_of(he, struct task_struct, he);
	return p;
}
