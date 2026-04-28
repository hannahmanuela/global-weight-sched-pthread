#ifndef _HEAP_ELEM_H_

#define _HEAP_ELEM_H_

#include <limits.h>
#include <stdbool.h>
#include "vt.h"

typedef short idx_t;

struct heap_elem {
	vt_t vruntime;
	w_t weight;
	void *elem;
};

typedef int (*cmp_elem_t)(struct heap_elem *, struct heap_elem*);

bool heap_elem_is_dummy(struct heap_elem *he);

#endif
