#ifndef _LHEAP_H_
#define _LHEAP_H_

//#include <pthread.h>

#include "util.h"
#include "core.h"
#include "lock.h"
#include "heap.h"

struct lheap {
	struct heap *heap;

	//pthread_rwlock_t heap_lock;
	struct spinlock lk __attribute__((aligned(CACHE_LINE_SZ)));
} __attribute__((aligned(CACHE_LINE_SZ)));

struct lheap *lh_new(int grp_cmp(struct heap_elem*,struct heap_elem*));
void lh_unlock(struct core *, struct lheap *lh);
void lh_lock(struct core *, struct lheap *lh);
int lh_try_lock(struct core *, struct lheap *lh);
void lh_stats(struct lheap *lh);

#endif
