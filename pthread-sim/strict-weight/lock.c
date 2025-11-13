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
	return atomic_load(&lk->locked);
}

void
lock_acquire(struct spinlock *lk)
{
	while (atomic_flag_test_and_set(&lk->locked))
		;
}

int
lock_try_acquire(struct spinlock *lk)
{
	int r = atomic_flag_test_and_set(&lk->locked);

	return r;
}


// Release the lock.
void
lock_release(struct spinlock *lk)
{
	atomic_flag_clear(&lk->locked);
}
