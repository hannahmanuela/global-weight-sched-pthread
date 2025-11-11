// Mutual exclusion spin locks.

#include <stddef.h>

#include "lock.h"
#include "util.h"

void
lock_init(struct spinlock *lk)
{
	lk->locked = 0;
	lk->c = 0;
}

// Check whether this cpu is holding the lock.
// Interrupts must be off.
int
holding(struct spinlock *lk)
{
	return lk->locked;
}

// Acquire the lock.
// Loops (spins) until the lock is acquired.
void
lock_acquire(struct spinlock *lk)
{
	// if(holding(lk))
		// error("acquire");

	while(__sync_lock_test_and_set(&lk->locked, 1) != 0)
		;

	// Tell the C compiler and the processor to not move loads or stores
	// past this point, to ensure that the critical section's memory
	// references happen strictly after the lock is acquired.
	__sync_synchronize();

	// Record info about lock acquisition for holding() and debugging.
	// lk->c = mycpu();
}

// Try to acquire lock; returns 0 on success; otherwise 1
int
lock_try_acquire(struct spinlock *lk)
{
	// if(holding(lk))
		// error("acquire");

	int r = __sync_lock_test_and_set(&lk->locked, 1);

	// Tell the C compiler and the processor to not move loads or stores
	// past this point, to ensure that the critical section's memory
	// references happen strictly after the lock is acquired.
	__sync_synchronize();

	// Record info about lock acquisition for holding() and debugging.
	// lk->c = mycpu();
	return r;
}


// Release the lock.
void
lock_release(struct spinlock *lk)
{
	if(!holding(lk))
		error("release");

	lk->c = NULL;

	// Tell the C compiler and the CPU to not move loads or stores
	// past this point, to ensure that all the stores in the critical
	// section are visible to other CPUs before the lock is released,
	// and that loads in the critical section occur strictly before
	// the lock is released.
	__sync_synchronize();

	// Release the lock, equivalent to lk->locked = 0.
	// This code doesn't use a C assignment, since the C standard
	// implies that an assignment might be implemented with
	// multiple store instructions.
	__sync_lock_release(&lk->locked);
}

