#ifndef _PREEMPT_H_

#define _PREEMPT_H_

#include "core.h"
#include "process.h"
#include "mheap.h"

void running_set(struct mheap *, struct task_struct *, int cid);
bool running_clear(struct mheap *, struct task_struct *);
int running_find_and_clear(struct mheap *);

#endif
