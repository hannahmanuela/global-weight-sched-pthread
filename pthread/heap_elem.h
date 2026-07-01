#ifndef _HEAP_ELEM_H_

#define _HEAP_ELEM_H_

#include <limits.h>
#include <stdbool.h>
#include "vt.h"

typedef short idx_t;

struct heap_elem {
	// key
	vt_t vruntime;
	w_t weight;

	idx_t idx;  // for remove

	// for logging
	long tsc_in;
	long tsc_out;
	short id;
};

// 1 if e0 < e2
// 0 if e0 >= e2
// -1 if e0 and e1 are dummies
typedef int (*is_lt_elem_t)(struct heap_elem *e0, struct heap_elem *e1);

#endif
