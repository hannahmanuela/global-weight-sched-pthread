#ifndef _HEAP_ELEM_H_

#define _HEAP_ELEM_H_

#include <limits.h>
#include "vt.h"

#define DUMMY (INT_MAX)   // if weight is DUMMY, then dummy heap_elem

struct heap_elem {
	vt_t vruntime;
	int weight;
	void *elem;
};


typedef int (*cmp_elem_t)(struct heap_elem *, struct heap_elem*);

bool heap_elem_is_dummy(struct heap_elem *he);

#endif
