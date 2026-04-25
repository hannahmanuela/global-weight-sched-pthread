#ifndef _MCOUNTER_H

#define _MCOUNTER_H_

#include "util.h"
#include "core.h"

struct cntr {
	long  cntr __calign__;
};

struct mcntr {
	struct cntr **c;
	int n;
};

struct mcntr *mc_new();
bool mc_is_zero(struct mcntr *, struct core *c);
void mc_inc(struct mcntr *, struct core *c);
void mc_dec(struct mcntr *, struct core *c);
long mc_val(struct mcntr *);

#endif
