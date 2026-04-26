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
	mc->c = (struct cntr **) aligned_alloc(CACHE_LINE_SZ, sizeof(struct cntr) * num_cores);
	for (int i = 0; i < num_cores; i++) {
		mc->c[i] = (struct cntr *) aligned_alloc(CACHE_LINE_SZ, (sizeof(struct cntr)));
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
	*i = c_rand(c, mc->n);
	*j = c_rand(c, mc->n);
	while (*i == *j) {
		c->nrand++;
		*j = c_rand(c, mc->n);
	}
	*v1 = atomic_load_explicit(&(mc->c[*i]->cntr), __ATOMIC_ACQUIRE);
	*v2 = atomic_load_explicit(&(mc->c[*j]->cntr), __ATOMIC_ACQUIRE);
}	

float mc_approx_val(struct mcntr *mc, struct core *c) {
	int i, j;
	long c0, c1;
	mc_two_bins(mc, c, &i, &j, &c0, &c1);
	float val = ((c0+c1) * mc->n)/2.0;
	return val;
}

bool mc_is_zero(struct mcntr *mc, struct core *c) {
	float val = mc_approx_val(mc, c);
	bool empty1 = val <= 0.1;   // XX maybe to strict?
	/*
	if(!empty1) 
		printf("val0 %0.2f\n", val0);
	*/
	/*
	long val = mc_val(mc);
	bool empty = (val == 0);
	if (empty1 != empty) {
		printf("core %d:%d %0.2f (%d,%d)\n", c->cid, val, val0, c0, c1);
		mc_print(mc);
	}
	*/
	c->nmc_is_zero += 1;
	return empty1;
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
