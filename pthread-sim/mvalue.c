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
	mv->value = (struct value **) aligned_alloc(CACHE_LINE_SZ, sizeof(struct value *) * n);
	for (int i = 0; i < n; i++) {
		mv->value[i] = aligned_alloc(CACHE_LINE_SZ, sizeof(struct value));
		lock_init(&mv->value[i]->lk);
		mv->value[i]->value = 0;
	}
	mv->nheap = n;
	return mv;
}

void mv_free(struct mvalue *mv) {
	for (int i = 0; i < mv->nheap; i++) {
		free(mv->value[i]);
	}
	free(mv->value);
	free(mv);
}

void mv_print(struct mvalue *mv) {
	printf("= mv:\n");
	for (int i = 0; i < mv->nheap; i++) {
		printf("  Value %d: %lld\n", i, mv->value[i]->value);
	}
	printf("=\n");
}

static void __attribute__ ((noinline)) mv_rand_values(struct mvalue *mv, struct core *c, int *i, int *j) {
	*i = c_rand(c, mv->nheap);
	*j = c_rand(c, mv->nheap);
	while (*i == *j) {
		c->nrand++;
		*j = c_rand(c, mv->nheap);
	}
}

static struct value * __attribute__ ((noinline)) mv_select(struct mvalue *mv, int i, int j, vt_t *vt) {
	struct value *v_i = mv->value[i];
	struct value *v_j = mv->value[j];
	vt_t vt_i = atomic_load_explicit((_Atomic vt_t *) &v_i->value, __ATOMIC_RELAXED);
	vt_t vt_j = atomic_load_explicit((_Atomic vt_t *) &v_j->value, __ATOMIC_RELAXED);
	struct value *v;
	if (vt_i <= vt_j) {
		*vt = vt_i;
		v = v_i;
	} else {
		*vt = vt_j;
		v = v_j;
	}
	return v;
}

// https://dl.acm.org/doi/10.1145/2755573.2755616
static vt_t mv_sample_val(struct mvalue *mv, struct core *c) {
	long r = 0;
	long r_lock = 0;
retry:
	int i, j;
	vt_t vt;

	mv_rand_values(mv, c, &i, &j);
	struct value *v = mv_select(mv, i, j, &vt);

	int l = lock_try_acquire(&v->lk);
	if (l != 0) {
		r++;
		goto retry;
	}
	vt_t vt0 = v->value;
	if (vt != vt0) {
		r_lock++;
		lock_release(&v->lk);
		goto retry;
	}
	vt_t out = v->value;
	lock_release(&v->lk);
	return out;
}

vt_t mv_get_val(struct mvalue *mv, struct core *c) {
	if (mv->nheap == 1) {
		struct value *v = mv->value[0];
		lock_acquire(&v->lk);
		vt_t out = v->value;
		lock_release(&v->lk);
		return out;
	}
	return mv_sample_val(mv, c);
}

void mv_set_val(struct mvalue *mv, struct core *c, vt_t new_val) {

	long r = 0;
	if(mv->nheap == 1) {
		struct value *v = mv->value[0];
		lock_acquire(&v->lk);
		v->value = new_val;
		lock_release(&v->lk);
	}
retry:
	int i = c_rand(c, mv->nheap);
	struct value *v = mv->value[i];
	if(lock_try_acquire(&v->lk) != 0) {
		r++;
		goto retry;
	}
	v->value = new_val;
	lock_release(&v->lk);
}
