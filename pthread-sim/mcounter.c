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
	if(c0 > 0) {
		return false;
	}
	int j = c_rand(c, mc->n);
	while (i == j) {
		c->nrand++;
		j = c_rand(c, mc->n);
	}
	c0 = atomic_load_explicit(&(mc->c[j]->cntr), __ATOMIC_RELAXED);
	if(c0 > 0) {
		return false;
	}
	return true;
}

void mc_inc(struct mcntr *mc, int cid) {
	atomic_fetch_add_explicit(&(mc->c[cid]->cntr), 1,  __ATOMIC_RELAXED);
}

void mc_dec(struct mcntr *mc, int cid) {
	atomic_fetch_add_explicit(&(mc->c[cid]->cntr), -1,  __ATOMIC_RELAXED);
}
