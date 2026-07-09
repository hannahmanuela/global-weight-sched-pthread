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
	is_lt_elem_t lt;
};

struct mheap *mh_new(int n, is_lt_elem_t lt);
void mh_stats(struct mheap *mh);
void mh_free(struct mheap *mh);
void mh_print(struct mheap *mh, void (*print_heap_elem)(struct heap_elem *));
struct heap *mh_heap(struct mheap *mh, int i);
vt_t mh_min_vt(struct heap *h);
vt_t mh_last_vt(struct heap *h);
struct heap_elem *mh_deq_min_elem(struct mheap *mh, int hint);
struct heap_elem *mh_deq_min_elem_enq(struct mheap *mh, struct heap_elem *p, int hint);
struct heap_elem *mh_deq_min_elem_all_heap(struct mheap *mh, is_min_elem_t minf, int hint);
struct heap *mh_choose_heap(struct mheap *mh);
float mh_load(struct mheap *mh, int *maxl);
void mh_rand_heaps(struct mheap *mh, int *i, int *j);
int mh_insert_elem(struct mheap *mh, struct heap_elem *e);
void mh_remove_elem(struct mheap *mh, int hi, struct heap_elem *e);

#endif
