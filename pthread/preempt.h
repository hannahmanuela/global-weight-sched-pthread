#ifndef _PREEMPT_H_

#define _PREEMPT_H_

#include <stdatomic.h>

#include "core.h"

#define NBITARRAY 1

typedef atomic_long bitarray_t[NBITARRAY];

void preemptable_set(bitarray_t, int cid);
bool preemptable_clear(bitarray_t, int cid);
int preemptable_find_and_clear(bitarray_t);
bool preemptable_is_set(bitarray_t, int cid);

static inline void aadd(int src, int dst) {
        // aadd: dst is a memory location, src is reg
	asm ("mov %1, %%eax; aadd %%eax, %0"
	     : "=m" (dst)
	     : "r" (src)
	     :"%eax" );
}

static inline void aor(int src, int dst) {
        // aadd: dst is a memory location, src is reg
	asm ("mov %1, %%eax; aor %%eax, %0"
	     : "=m" (dst)
	     : "r" (src)
	     :"%eax" );
}

#endif
