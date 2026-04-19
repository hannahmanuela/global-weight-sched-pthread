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
};

struct mheap *mh_new(int n);
void mh_free(struct mheap *mh);
void mh_print(struct mheap *mh);
vt_t mh_min_vt(struct heap *h);
struct process *mh_min_proc(struct mheap *mh, struct core *c);
struct heap *mh_choose_heap( struct mheap *mh, struct core *c);
void mh_add_process(struct core *c, struct process *p, struct heap *h);
struct process *mh_min_affinity(struct core *c);

#endif
