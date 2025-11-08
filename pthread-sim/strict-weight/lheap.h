#ifndef _LHEAP_H_
#define _LHEAP_H_

#include <pthread.h>
#include <stdatomic.h>

#include "heap.h"

struct lheap {
	pthread_rwlock_t heap_lock;
	struct heap *heap;
	long wait_for_wr_heap_lock_cycles;
	long num_times_wr_heap_locked;
	atomic_long wait_for_rd_heap_lock_cycles;
	atomic_long num_times_rd_heap_locked;
	long insert_cycles;
	long remove_cycles;
	long ninsert;
	long nremove;
} __attribute__((aligned(64)));

struct lheap *lh_new(int grp_cmp(void*,void*));
void lh_unlock(struct lheap *lh);
void lh_lock(struct lheap *lh);
void lh_lock_timed(struct lheap *lh);
int lh_try_lock_timed(struct lheap *lh);
void lh_rdlock_timed(struct lheap *lh);
void lh_stats(struct lheap *lh);
int lh_try_lock(struct lheap *lh);
void *lh_min_atomic(struct lheap *lh);
int lh_avg_spec_virt_time_inc(struct lheap *lh);

#endif
