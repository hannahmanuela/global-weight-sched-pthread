#ifndef _MHEAP_H_

#define _MHEAP_H_

#include "util.h"
#include "heap_elem.h"
#include "core.h"
#include "group.h"
#include "heap.h"

struct mheap {
	struct heap **h;
	int nheap;
	int tick_length;
} __attribute__((aligned(CACHE_LINE_SZ)));

struct mheap *mh_new(int grpcmp(struct heap_elem *, struct heap_elem *), int n, int tick_length); 
void mh_free(struct mheap *mh);
void mh_print(struct mheap *mh);
vt_t mh_min_vt(struct heap *h);
struct process *mh_min_proc(struct core *c, struct mheap *mh);
struct heap *mh_choose_heap(struct core *c, struct mheap *mh);
void mh_add_process(struct core *c, struct process *p, struct heap *h);

#endif
