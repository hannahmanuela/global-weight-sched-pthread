#include <assert.h>
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdatomic.h>

#include "util.h"
#include "vt.h"
#include "group.h"
#include "lheap.h"

struct lock_heap *lh_new(int grp_cmp(void *, void*)) {
	struct lock_heap *lh = (struct lock_heap *) malloc(sizeof(struct lock_heap));
	lh->heap = heap_new(grp_cmp);
	lh->wait_for_wr_heap_lock_cycles = 0;
	lh->num_times_wr_heap_locked = 0;
	atomic_init(&lh->wait_for_rd_heap_lock_cycles, 0);
	atomic_init(&lh->num_times_rd_heap_locked, 0);
	pthread_rwlock_init(&lh->heap_lock, NULL);
	return lh;
}

void lh_stats(struct lock_heap *lh) {
	if (lh->num_times_wr_heap_locked > 0) {
		printf("Group list write lock: avg %ld cycles (%ld total cycles, %ld operations)\n", 
		       lh->wait_for_wr_heap_lock_cycles / lh->num_times_wr_heap_locked,
		       lh->wait_for_wr_heap_lock_cycles, lh->num_times_wr_heap_locked);
	}
	if (lh->num_times_rd_heap_locked > 0) {
		printf("Group list read lock: avg %ld cycles (%ld total cycles, %ld operations)\n", 
		       lh->wait_for_rd_heap_lock_cycles / lh->num_times_rd_heap_locked,
		       lh->wait_for_rd_heap_lock_cycles, lh->num_times_rd_heap_locked);
	}
}

void lh_unlock(struct lock_heap *lh) {
	pthread_rwlock_unlock(&lh->heap_lock);
}

void lh_lock(struct lock_heap *lh) {
	pthread_rwlock_wrlock(&lh->heap_lock);
}

// if l = 0,  successful lock
int lh_try_lock(struct lock_heap *lh) {
	int l = pthread_rwlock_trywrlock(&lh->heap_lock);
	return l;
}

void lh_rdlock(struct lock_heap *lh) {
	pthread_rwlock_rdlock(&lh->heap_lock);
}

// Wrapper functions for pthread_rwlock operations with timing
void lh_lock_timed(struct lock_heap *lh) {
	int start_tsc = safe_read_tsc();
	lh_lock(lh);
	int end_tsc = safe_read_tsc();
	lh->wait_for_wr_heap_lock_cycles += (end_tsc - start_tsc);
	lh->num_times_wr_heap_locked++;
}

void lh_rdlock_timed(struct lock_heap *lh) {
	int start_tsc = safe_read_tsc();
	lh_rdlock(lh);
	int end_tsc = safe_read_tsc();
	atomic_fetch_add_explicit(&lh->wait_for_rd_heap_lock_cycles, (end_tsc - start_tsc), memory_order_relaxed);
	atomic_fetch_add_explicit(&lh->num_times_rd_heap_locked, 1, memory_order_relaxed);
}
