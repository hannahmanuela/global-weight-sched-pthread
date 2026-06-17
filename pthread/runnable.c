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
#include "heap.h"
#include "mheap.h"
#include "util.h"

//
// queue of runnable processes using mheap
//

struct task_struct *runnable_deq_proc_hint(struct mheap *mh, struct task_struct *prev, struct heap *hint) {
	struct heap_elem *he = mh_deq_min_elem_enq(mh, &prev->he, hint);
	return container_of(he, struct task_struct, he);
}

struct task_struct *runnable_deq_proc(struct mheap *mh, struct task_struct *prev) {
	return runnable_deq_proc_hint(mh, prev, NULL);
}
