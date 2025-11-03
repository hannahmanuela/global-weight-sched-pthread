#include "group.h"
#include "heap.h"
#include "lheap.h"

#ifndef MHEAP_H
#define MHEAP_H

struct mheap {
	int nheap;
	int tick_length;
	struct lock_heap **lh;
};

struct mheap *mh_new(int grpcmp(void *, void *), int n, int seed, int tick_length); 
void mh_print(struct mheap *mh);
int mh_min_vrt(struct lock_heap *lh);
void mh_lock_stats(struct mheap *mh);
void mh_runtime_stats(struct mheap *mh);
struct lock_heap *mh_heap(struct mheap *, int i);
struct group_shard *mh_min_group_shard(struct mheap *mh);
void mh_check_min_group_shard(struct mheap *mh, struct group_shard *s);
struct lock_heap *mh_choose_heap(struct mheap *mh);
void mh_add_group_shard(struct group_shard *s, struct lock_heap *lh);
void mh_del_group_shard(struct mheap *mh, struct group_shard *s);
#endif
