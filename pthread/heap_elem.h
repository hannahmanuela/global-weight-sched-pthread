#ifndef _HEAP_ELEM_H_

#define _HEAP_ELEM_H_

#include <limits.h>
#include <stdbool.h>
#include <stdatomic.h>
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

// is_lt_elem returns:
// 1 if e0 < e2
// 0 if e0 >= e2
// -1 if e0 and e1 are dummies
typedef int (*is_lt_elem_t)(struct heap_elem *e0, struct heap_elem *e1);

// is_min_elemn returns:
// 1 if true
// 0, otherwise 
typedef int (*is_min_elem_t)(struct heap_elem *e0);

static int is_lt_elem_vt_w(struct heap_elem *he_i, struct heap_elem *he_j) {

	vt_t vt_i = atomic_load_explicit(&he_i->vruntime, __ATOMIC_RELAXED);
	vt_t vt_j = atomic_load_explicit(&he_j->vruntime, __ATOMIC_RELAXED);
	if ((vt_i == DUMMY) && (vt_j == DUMMY)) {
		return -1;
	}
	if (vt_i == DUMMY) {
		return 0;
	} 
	if (vt_i > vt_j) {
		return 1;
	} else if (vt_i == vt_j) {
		int w_i = atomic_load_explicit(&he_i->weight, __ATOMIC_RELAXED);
		int w_j = atomic_load_explicit(&he_j->weight, __ATOMIC_RELAXED);
		if (w_j > w_i) {	
			return 0;
		}
	}
	return 1;
}

static int is_min_elem_vt(struct heap_elem *he) {
	vt_t vt = atomic_load_explicit(&he->vruntime, __ATOMIC_RELAXED);
	return vt != DUMMY;
}

typedef void (*print_elem_t)(struct heap_elem *he);

static void print_elem_vt(struct heap_elem *he) {
	if(he->vruntime == DUMMY) {
		printf("[dummy vt %lld w %d]", he->vruntime, he->weight);
		return;
	}
	printf("[vt %lld w %d]", he->vruntime, he->weight);
}


#endif
