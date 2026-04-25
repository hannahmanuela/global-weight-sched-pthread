#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>

#include "util.h"
//#include "core.h"
#include "mcounter.h"

// XXX maybe use AADD or AOR

extern int num_cores;

struct mcntr *mc_new() {
	struct mcntr *mc = malloc(sizeof(struct mcntr));
	mc->c = (struct cntr **) aligned_alloc(CACHE_LINE_SZ, sizeof(struct cntr) * num_cores);
	for (int i = 0; i < num_cores; i++) {
		mc->c[i] = (struct cntr *) aligned_alloc(CACHE_LINE_SZ, (sizeof(struct cntr)));
	}
	mc->n = num_cores;
	return mc;
}

bool mc_is_zero(struct mcntr *mc, struct core *c) {
	int i = c_rand(c, mc->n);
	long c0 = atomic_load_explicit(&(mc->c[i]->cntr), __ATOMIC_RELAXED);
	int j = c_rand(c, mc->n);
	while (i == j) {
		c->nrand++;
		j = c_rand(c, mc->n);
	}
	long c1 = atomic_load_explicit(&(mc->c[j]->cntr), __ATOMIC_RELAXED);
	float val0 = ((c0+c1) * mc->n)/2.0;
	bool empty1 = val0 <= (num_cores/2);
	//long val = mc_val(mc);
	//bool empty = (val == 0);
	//if (empty && !empty1)
	// printf("core %d:%d %0.2f (%d,%d)\n", c->cid, val, val0, c0, c1);
	return empty1;
}

void mc_inc(struct mcntr *mc, struct core *c) {
	int i = c_rand(c, mc->n);
	atomic_fetch_add_explicit(&(mc->c[i]->cntr), 1,  __ATOMIC_RELAXED);
}

void mc_dec(struct mcntr *mc, struct core *c) {
	int i = c_rand(c, mc->n);
	atomic_fetch_add_explicit(&(mc->c[i]->cntr), -1,  __ATOMIC_RELAXED);
}

long mc_val(struct mcntr *mc) {
	long val = 0;
	for (int i = 0; i < mc->n; i++) {
		long v = atomic_load_explicit(&(mc->c[i]->cntr),  __ATOMIC_RELAXED);
		val += v;
	}
	return val;
}
