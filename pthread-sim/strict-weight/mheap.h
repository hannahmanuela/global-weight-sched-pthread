#include "group.h"
#include "heap.h"

struct mheap {
	int total_weight;
	int nheap;
	struct lock_heap **lh;
};

struct mheap *mh_new(int grpcmp(void *, void *), int n); 
int mh_empty(struct group *g);
int mh_time(struct mheap *mh, int tick_length, int w);
int mh_time_inc(struct mheap *mh, int tick_length, int w);
void mh_print(struct mheap *mh, void print(struct heap_elem *));
void mh_lock_stats(struct mheap *mh);
void mh_runtime_stats(struct mheap *mh);
struct lock_heap *mh_heap(struct mheap *, int i);
struct group *mh_min_group(struct mheap *mh);
void mh_check_min_group(struct mheap *mh, struct group *g);
void mh_add_group(struct mheap *mh, struct group *g);
void mh_del_group(struct mheap *mh, struct group *g);

