#include <assert.h>
#include <stddef.h>
#include <stdatomic.h>

#include "lock.h"
#include "util.h"
#include "core.h"

void
lock_init(struct spinlock *lk)
{
	lk->holder = NULL;
	atomic_flag_clear(&lk->locked);
}

void*
lock_holder(struct spinlock *lk) {
	void *p = atomic_load(&lk->holder);
	return p;
}

int
lock_holding(struct spinlock *lk)
{
	int b = atomic_load_explicit(&lk->locked, __ATOMIC_RELAXED);
	return b;
}

void
lock_acquire(struct spinlock *lk)
{
	while (atomic_flag_test_and_set_explicit(&lk->locked, __ATOMIC_ACQUIRE))
		;
	assert(lk->holder == NULL);
	lk->holder = mycore();
}

int
lock_try_acquire(struct spinlock *lk)
{
	int r = atomic_flag_test_and_set_explicit(&lk->locked,  __ATOMIC_ACQUIRE);
	if(r == 0) {
		lk->holder = mycore();
	}
	return r;
}

void
lock_release(struct spinlock *lk)
{
	assert(lk->holder == mycore());
	lk->holder = NULL;
	atomic_flag_clear_explicit(&lk->locked, __ATOMIC_RELEASE);
}
