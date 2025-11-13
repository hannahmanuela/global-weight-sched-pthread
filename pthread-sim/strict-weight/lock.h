#ifndef _LOCK_H_

#define _LOCK_H_

#include <stdint.h>

#include "core.h"

// Mutual exclusion lock.
struct spinlock {
  uint32_t locked;       // Is the lock held?
  struct core *c;   // The cpu holding the lock.
};

void lock_init(struct spinlock *lk);
void lock_acquire(struct spinlock *lk);
int lock_try_acquire(struct spinlock *lk);
void lock_release(struct spinlock *lk);
int lock_holding(struct spinlock *lk);
#endif
