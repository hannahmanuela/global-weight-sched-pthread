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
void lh_unlock(struct lheap *lh);
void lh_lock(struct lheap *lh);
void lh_stats(struct lheap *lh);
int lh_try_lock(struct lheap *lh);
int lh_avg_spec_virt_time_inc(struct lheap *lh);
void lh_lock_timed(struct core *c, struct lheap *lh);
int lh_try_lock_timed(struct core *c, struct lheap *lh);

#endif
