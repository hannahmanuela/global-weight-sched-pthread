#include <assert.h>
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdatomic.h>

#include "util.h"
#include "vt.h"
#include "group.h"
#include "lheap.h"

struct lheap *lh_new(int grp_cmp(struct heap_elem *, struct heap_elem*)) {
	struct lheap *lh = (struct lheap *) malloc(sizeof(struct lheap));
	lh->heap = heap_new(grp_cmp);
	lh->wait_for_wr_heap_lock_cycles = 0;
	lh->num_times_wr_heap_locked = 0;
	lh->remove_cycles = 0;
	lh->insert_cycles = 0;
	lh->ninsert = 0;
	lh->nremove = 0;
	atomic_init(&lh->wait_for_rd_heap_lock_cycles, 0);
	atomic_init(&lh->num_times_rd_heap_locked, 0);
	pthread_rwlock_init(&lh->heap_lock, NULL);
	return lh;
}

void lh_stats(struct lheap *lh) {
	if ((lh->num_times_wr_heap_locked > 0) || (lh->num_times_rd_heap_locked > 0))
		printf("== heap %p:\n", lh);
	if (lh->num_times_wr_heap_locked > 0) {
		printf("Heap write lock: avg %ld cycles (%ld total cycles, %ld operations)\n", 
		       lh->wait_for_wr_heap_lock_cycles / lh->num_times_wr_heap_locked,
		       lh->wait_for_wr_heap_lock_cycles, lh->num_times_wr_heap_locked);
	}
	if (lh->num_times_rd_heap_locked > 0) {
		printf("Heap read lock: avg %ld cycles (%ld total cycles, %ld operations)\n", 
		       lh->wait_for_rd_heap_lock_cycles / lh->num_times_rd_heap_locked,
		       lh->wait_for_rd_heap_lock_cycles, lh->num_times_rd_heap_locked);
	}
}

void lh_unlock(struct lheap *lh) {
	pthread_rwlock_unlock(&lh->heap_lock);
}

void lh_lock(struct lheap *lh) {
	pthread_rwlock_wrlock(&lh->heap_lock);
}

// if l = 0,  successful acquire
int lh_try_lock(struct lheap *lh) {
	int l = pthread_rwlock_trywrlock(&lh->heap_lock);
	return l;
}

void lh_rdlock(struct lheap *lh) {
	pthread_rwlock_rdlock(&lh->heap_lock);
}

