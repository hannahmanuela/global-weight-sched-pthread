#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
#include <limits.h>

#include "vt.h"
#include "core.h"
#include "lock.h"
#include "mvalue.h"
#include "util.h"

struct mvalue *mv_new(int n) {
	struct mvalue *mv = malloc(sizeof(struct mvalue));
	mv->value = (vt_t **) aligned_alloc(CACHE_LINE_SZ, sizeof(vt_t *) * n);
	for (int i = 0; i < n; i++) {
		mv->value[i] = aligned_alloc(CACHE_LINE_SZ, CACHE_LINE_SZ); // vt size < cache line sz?
		*mv->value[i] = 0;
	}
	mv->nvalues = n;
	return mv;
}

void mv_free(struct mvalue *mv) {
	for (int i = 0; i < mv->nvalues; i++) {
		free(mv->value[i]);
	}
	free(mv->value);
	free(mv);
}

void mv_print(struct mvalue *mv) {
	printf("= mv:\n");
	for (int i = 0; i < mv->nvalues; i++) {
		printf("  Value %d: %lld\n", i, *mv->value[i]);
	}
	printf("=\n");
}

static void __attribute__ ((noinline)) mv_rand_values(struct mvalue *mv, struct core *c, int *i, int *j) {
	assert(mv->nvalues >= 2);
	*i = c_rand(c, mv->nvalues);
	*j = c_rand(c, mv->nvalues);
	while (*i == *j) {
		c->nrand++;
		*j = c_rand(c, mv->nvalues);
	}
}


static vt_t mv_sample_val(struct mvalue *mv, struct core *c) {
	int i, j;
	vt_t vt;

	mv_rand_values(mv, c, &i, &j);
	vt_t vt_i = atomic_load_explicit((_Atomic vt_t *) mv->value[i], __ATOMIC_RELAXED);
	vt_t vt_j = atomic_load_explicit((_Atomic vt_t *) mv->value[j], __ATOMIC_RELAXED);
	if (vt_i <= vt_j) {
		vt = vt_i;
	} else {
		vt = vt_j;
	}
	return vt;
}

vt_t mv_get_val(struct mvalue *mv, struct core *c) {
	if (mv->nvalues == 1) {
		return atomic_load_explicit((_Atomic vt_t *) mv->value[0], __ATOMIC_RELAXED);
	}
	return mv_sample_val(mv, c);
}

void mv_set_val(struct mvalue *mv, struct core *c, vt_t new_val) {

	if(mv->nvalues == 1) {
		atomic_store(mv->value[0], new_val);
		return;
	}
	int i = c_rand(c, mv->nvalues);
	atomic_store(mv->value[i], new_val);
}
