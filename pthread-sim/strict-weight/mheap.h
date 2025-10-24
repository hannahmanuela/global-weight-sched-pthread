#include "group.h"
#include "heap.h"

struct mheap {
	int nheap;
	struct lock_heap **lh;
};

struct mheap *mh_new(int grpcmp(void *, void *), int n); 
int mh_empty(struct group *g);
void mh_print(struct mheap *mh, void print(struct heap_elem *));
void mh_stats(struct mheap *mh);
struct lock_heap *mh_heap(struct mheap *, int i);
struct group *mh_min_group(struct mheap *mh);
void mh_check_min_group(struct mheap *mh, struct group *g);
void mh_add_group(struct mheap *mh, struct group *g);
void mh_del_group(struct mheap *mh, struct group *g);

