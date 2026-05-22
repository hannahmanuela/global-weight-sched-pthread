#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>

#include "util.h"
#include "mcounter.h"

// XXX maybe use AADD or AOR

extern int num_cores;

struct mcntr *mc_new() {
	struct mcntr *mc = malloc(sizeof(struct mcntr));
	mc->c = (struct cntr **) aligned_alloc(CACHE_LINE_SZ, ALIGN_UP(sizeof(struct cntr) * num_cores, CACHE_LINE_SZ));
	for (int i = 0; i < num_cores; i++) {
		mc->c[i] = (struct cntr *) aligned_alloc(CACHE_LINE_SZ, ALIGN_UP(sizeof(struct cntr), CACHE_LINE_SZ));
	}
	mc->n = num_cores;
	return mc;
}

long mc_print(struct mcntr *mc) {
	printf("mc:");
	for (int i = 0; i < mc->n; i++) {
		long v = atomic_load_explicit(&(mc->c[i]->cntr),  __ATOMIC_ACQUIRE);
		printf("%d ", v);
	}
	printf("\n");
}

void mc_two_bins(struct mcntr *mc, struct core *c, int *i, int *j, long *v1, long *v2) {
	*i = c_rand(mc->n);
	*j = c_rand(mc->n);
	while (*i == *j) {
		c->nrand++;
		*j = c_rand(mc->n);
	}
	*v1 = atomic_load_explicit(&(mc->c[*i]->cntr), __ATOMIC_ACQUIRE);
	*v2 = atomic_load_explicit(&(mc->c[*j]->cntr), __ATOMIC_ACQUIRE);
}	

float mc_approx_val(struct mcntr *mc, struct core *c) {
	int i, j;
	long c0, c1;
	mc_two_bins(mc, c, &i, &j, &c0, &c1);
	float val = ((c0+c1) * mc->n);
	return val;
}

#define MC_IND(mc, i) ((i) % mc->n)

bool mc_is_zero(struct mcntr *mc, struct core *c) {
	c->nmc_is_zero += 1;
	int s = c_rand(mc->n);
	for (int i = 0; i < mc->n; i++) {
		struct cntr *c = mc->c[MC_IND(mc,s+i)];
		int v = atomic_load_explicit(&c->cntr, __ATOMIC_ACQUIRE);
		if(v != 0) {
			return false;
		}

	}
	return true;
}

void mc_inc(struct mcntr *mc, struct core *c) {
	int i, j;
	long c0, c1;
	mc_two_bins(mc, c, &i, &j, &c0, &c1);
	// add the lowest bin
	if (c0 < c1) atomic_fetch_add_explicit(&(mc->c[i]->cntr), 1,  __ATOMIC_ACQUIRE);
	else atomic_fetch_add_explicit(&(mc->c[j]->cntr), 1,  __ATOMIC_ACQUIRE);
	c->nmc_inc += 1;
}

void mc_dec(struct mcntr *mc, struct core *c) {
	int i, j;
	long c0, c1;
	mc_two_bins(mc, c, &i, &j, &c0, &c1);
	// remove from the highest bin
	if (c0 < c1) atomic_fetch_add_explicit(&(mc->c[j]->cntr), -1,  __ATOMIC_ACQUIRE);
	else atomic_fetch_add_explicit(&(mc->c[i]->cntr), -1,  __ATOMIC_ACQUIRE);
	c->nmc_dec += 1;
}

long mc_val(struct mcntr *mc) {
	long val = 0;
	for (int i = 0; i < mc->n; i++) {
		long v = atomic_load_explicit(&(mc->c[i]->cntr),  __ATOMIC_ACQUIRE);
		val += v;
	}
	return val;
}
