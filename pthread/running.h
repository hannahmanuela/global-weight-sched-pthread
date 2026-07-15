#ifndef _RUNNING_H_

#define _RUNNING_H_

#include "core.h"
#include "process.h"
#include "mheap.h"

void running_enq(struct mheap *, struct task_struct *, int cid);
void running_rm(struct mheap *, struct task_struct *);
int running_find_cid_deq(struct mheap *);
int running_find_cid_deq_all(struct mheap *);

#endif
