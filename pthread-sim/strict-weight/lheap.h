#include <pthread.h>
#include <stdatomic.h>

#include "heap.h"

struct lock_heap {
	pthread_rwlock_t heap_lock;
	struct heap *heap;
	long wait_for_wr_heap_lock_cycles;
	long num_times_wr_heap_locked;
	atomic_long wait_for_rd_heap_lock_cycles;
	atomic_long num_times_rd_heap_locked;
} __attribute__((aligned(64)));

struct lock_heap *lh_new(int grp_cmp(void*,void*));
void lh_unlock(struct lock_heap *lh);
void lh_lock(struct lock_heap *lh);
void lh_lock_timed(struct lock_heap *lh);
void lh_rdlock_timed(struct lock_heap *lh);
void lh_stats(struct lock_heap *lh);
int lh_try_lock(struct lock_heap *lh);
void *lh_min_atomic(struct lock_heap *lh);
int lh_avg_spec_virt_time_inc(struct lock_heap *lh);
