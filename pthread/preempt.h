#ifndef _PREEMPT_H_

#define _PREEMPT_H_

#include <stdatomic.h>

#include "core.h"

#define NBITARRAY 1

typedef atomic_uint bitarray_t[NBITARRAY];

bool preemptable_set(bitarray_t, int cid, struct core *c);
bool preemptable_clear(bitarray_t, int cid, struct core *c);
int preemptable_find_and_clear(bitarray_t, struct core *c);

#endif
