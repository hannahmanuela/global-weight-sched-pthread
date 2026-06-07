#ifndef _MHEAP_H_

#define _MHEAP_H_

#include "util.h"
#include "heap_elem.h"
#include "core.h"
#include "group.h"
#include "heap.h"

struct mheap {
	struct heap **h __calign__;
	int nheap;
};

struct mheap *mh_new(int n);
void mh_stats(struct mheap *mh);
void mh_free(struct mheap *mh);
void mh_print(struct mheap *mh);
void mh_check_notlocked(struct mheap *mh);
struct heap *mh_heap(struct mheap *mh, int i);
vt_t mh_min_vt(struct heap *h);
vt_t mh_last_vt(struct heap *h);
struct task_struct *mh_min_proc(struct mheap *mh, bool all);
struct task_struct *mh_min_proc_enq(struct mheap *mh, struct task_struct *p, bool all);
struct heap *mh_choose_heap(struct mheap *mh);
struct task_struct *mh_min_affinity(struct core *c);
float mh_load(struct mheap *mh, int *maxl);
void mh_rand_heaps(struct mheap *mh, int *i, int *j);
void mh_insert_proc(struct mheap *mh, struct task_struct *p);

#endif
