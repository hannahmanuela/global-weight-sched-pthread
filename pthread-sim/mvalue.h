#ifndef _MVALUE_H_

#define _MVALUE_H_

#include "util.h"
#include "vt.h"
#include "lock.h"

struct core;

struct value {
	struct spinlock lk __calign__;

	vt_t value;
} __calign__;

struct mvalue {
	struct value **value;
	int nheap;
};

struct mvalue *mv_new(int n);
void mv_free(struct mvalue *mv);
void mv_print(struct mvalue *mv);
vt_t mv_get_val(struct mvalue *mv, struct core *c);
void mv_set_val(struct mvalue *mv, struct core *c, vt_t new_val);

#endif
