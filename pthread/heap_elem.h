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

typedef int (*cmp_elem_t)(struct heap_elem *, struct heap_elem*);

bool heap_elem_is_dummy(struct heap_elem *he);

#endif
