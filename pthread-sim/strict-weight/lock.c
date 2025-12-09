#include <assert.h>
#include <stddef.h>
#include <stdatomic.h>

#include "lock.h"
#include "util.h"

void
lock_init(struct spinlock *lk)
{
	atomic_flag_clear(&lk->locked);
}

int
lock_holding(struct spinlock *lk)
{
	// return lk->locked;
	return atomic_load_explicit(&lk->locked, __ATOMIC_RELAXED);
}

void
lock_acquire(struct spinlock *lk)
{
	while (atomic_flag_test_and_set_explicit(&lk->locked, __ATOMIC_ACQUIRE))
		;
}

int
lock_try_acquire(struct spinlock *lk)
{
	int r = atomic_flag_test_and_set_explicit(&lk->locked,  __ATOMIC_ACQUIRE);
	return r;
}


void
lock_release(struct spinlock *lk)
{
	atomic_flag_clear_explicit(&lk->locked, __ATOMIC_RELEASE);
}
