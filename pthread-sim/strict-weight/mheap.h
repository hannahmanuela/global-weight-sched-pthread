#include "group.h"
#include "heap.h"
#include "lheap.h"

struct mheap {
	int nheap;
	struct lock_heap **lh;
};

struct mheap *mh_new(int grpcmp(void *, void *), int n, int seed); 
int mh_empty(struct group *g);
void mh_print(struct mheap *mh);
int mh_min(struct lock_heap *lh);
void mh_lock_stats(struct mheap *mh);
void mh_runtime_stats(struct mheap *mh);
struct lock_heap *mh_heap(struct mheap *, int i);
struct group *mh_min_group(struct mheap *mh);
void mh_check_min_group(struct mheap *mh, struct group *g);
void mh_add_group(struct mheap *mh, struct group *g);
void mh_del_group(struct mheap *mh, struct group *g);

