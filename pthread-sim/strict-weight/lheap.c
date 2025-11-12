#include <assert.h>
#include <stdio.h>
//#include <pthread.h>
#include <stdlib.h>
#include <stdatomic.h>

#include "util.h"
#include "vt.h"
#include "core.h"
#include "group.h"
#include "lock.h"
#include "lheap.h"

struct lheap *lh_new(int grp_cmp(struct heap_elem *, struct heap_elem*)) {
	struct lheap *lh = aligned_alloc(CACHE_LINE_SZ, (sizeof(struct lheap)));
	lh->heap = heap_new(grp_cmp);
	// pthread_rwlock_init(&lh->heap_lock, NULL);
	lock_init(&lh->lk);
	assert(((long) &lh->heap) % CACHE_LINE_SZ == 0);
	assert(((long) &lh->lk) % CACHE_LINE_SZ == 0);
	return lh;
}

void lh_unlock(struct core *c, struct lheap *lh) {
	// pthread_rwlock_unlock(&lh->heap_lock);
	lock_release(&lh->lk);
}

void lh_lock(struct core *c, struct lheap *lh) {
	lock_acquire(&lh->lk);
	//pthread_rwlock_wrlock(&lh->heap_lock);
}

// if l = 0,  successful acquire
int lh_try_lock(struct core *c, struct lheap *lh) {
	// int l = pthread_rwlock_trywrlock(&lh->heap_lock);
	int l = lock_try_acquire(&lh->lk);
	return l;
}

