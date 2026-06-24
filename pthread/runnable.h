#ifndef _RUNNABLE_H

#define _RUNNABLE_H

#include "core.h"
#include "process.h"
#include "mheap.h"

struct task_struct *runnable_deq_proc(struct mheap *mh, struct task_struct*);
struct task_struct *runnable_deq_proc_hint(struct mheap *mh, struct task_struct*, struct heap *hint);
struct task_struct *runnable_deq_proc_all_heap(struct mheap *mh);

#endif
