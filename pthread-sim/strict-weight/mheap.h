#include "group.h"
#include "heap.h"
#include "lheap.h"

struct mheap {
	int nheap;
	int tick_length;
	struct lheap **lh;
};

struct mheap *mh_new(int grpcmp(void *, void *), int n, int seed, int tick_length); 
void mh_free(struct mheap *mh);
int mh_empty(struct group *g);
void mh_print(struct mheap *mh);
int mh_min(struct lheap *lh);
void mh_lock_stats(struct mheap *mh);
void mh_runtime_stats(struct mheap *mh);
struct lheap *mh_heap(struct mheap *, int i);
struct process *mh_min_proc(struct mheap *mh);
void mh_check_min_process(struct mheap *mh, struct process *g);
struct lheap *mh_choose_heap(struct mheap *mh);
void mh_add_process(struct process *p, struct lheap *lh);
void mh_del_process(struct mheap *mh, struct process *p);

