#include <assert.h>
#include <stdio.h>
//#include <pthread.h>
#include <stdlib.h>
#include <stdatomic.h>

#include "util.h"
#include "vt.h"
#include "group.h"
#include "lock.h"
#include "lheap.h"

extern bool with_tsc;

struct lheap *lh_new(int grp_cmp(struct heap_elem *, struct heap_elem*)) {
	struct lheap *lh = (struct lheap *) malloc(sizeof(struct lheap));
	lh->heap = heap_new(grp_cmp);
	// pthread_rwlock_init(&lh->heap_lock, NULL);
	lock_init(&lh->lk);
	return lh;
}

void lh_unlock(struct lheap *lh) {
	// pthread_rwlock_unlock(&lh->heap_lock);
	lock_release(&lh->lk);
}

void lh_lock(struct lheap *lh) {
	lock_acquire(&lh->lk);
	//pthread_rwlock_wrlock(&lh->heap_lock);
}

// if l = 0,  successful acquire
int lh_try_lock(struct lheap *lh) {
	// int l = pthread_rwlock_trywrlock(&lh->heap_lock);
	int l = lock_try_acquire(&lh->lk);
	return l;
}

// Wrapper functions for pthread_mutex operations with timing
void lh_lock_timed(struct core *c, struct lheap *lh) {
	if (with_tsc) {
		long start_tsc = safe_read_tsc();
		lh_lock(lh);
		long end_tsc = safe_read_tsc();
		c->wait_for_wr_heap_lock_cycles += (end_tsc - start_tsc);
		c->num_times_wr_heap_locked++;
	} else {
		lh_lock(lh);
	}
}

int lh_try_lock_timed(struct core *c, struct lheap *lh) {
	int l; 
	if (with_tsc) {
		long start_tsc = safe_read_tsc();
		l = lh_try_lock(lh);
		long end_tsc = safe_read_tsc();
		c->wait_for_wr_heap_lock_cycles += (end_tsc - start_tsc);
		c->num_times_wr_heap_locked++;
	} else {
		l = lh_try_lock(lh);
	}
	return l;
}
