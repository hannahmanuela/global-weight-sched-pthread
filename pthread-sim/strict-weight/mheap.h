#ifndef _MHEAP_H_

#define _MHEAP_H_

#include "heap_elem.h"
#include "core.h"
#include "group.h"
#include "heap.h"
#include "lheap.h"

struct mheap {
	int nheap;
	int tick_length;
	struct lheap **lh;
} __attribute__((aligned(64)));

struct mheap *mh_new(int grpcmp(struct heap_elem *, struct heap_elem *), int n, int tick_length); 
void mh_free(struct mheap *mh);
int mh_empty(struct group *g);
void mh_print(struct mheap *mh);
vt_t mh_min_vt(struct lheap *lh);
struct lheap *mh_heap(struct mheap *, int i);
struct process *mh_min_proc(struct core *c, struct mheap *mh);
struct lheap *mh_choose_heap(struct core *c, struct mheap *mh);
void mh_add_process(struct core *c, struct process *p, struct lheap *lh);
void mh_del_process(struct core *c, struct mheap *mh, struct process *p);

#endif
