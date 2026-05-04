
#include <stdio.h>
#include <stdatomic.h>

#include "preempt.h"
#include "core.h"

//
// XXX implement with new reduction instructions (AOR)?
//

#define CID(i, bit)  ((i) * (sizeof(unsigned int) * 8) + bit - 1)
#define BAINDEX(cid) ((cid) / 8)
#define BAOFFSET(cid) ((cid) % sizeof(unsigned int))

bool preemptable_set(bitarray_t ba, int cid, struct core *c) {
	unsigned int r = atomic_fetch_or(&ba[0], (1 << cid));
	bool set = r & (1 << cid);
	if (!set) {
		c->npreempt_set++;
	}
	return !set;
}

bool preemptable_clear(bitarray_t ba, int cid, struct core *c) {
	unsigned int mask = ~(1U << cid);
	unsigned int r = atomic_fetch_and(&ba[0], mask);
	bool ok = r & (1 << cid);
	if(ok) {
		c->npreempt_clear++;
	}
	return ok;
}

int preemptable_find_and_clear(bitarray_t ba, struct core *c) {
	int cid = -1;
	bool ok = false;
	while (!ok) {
		for (int i = 0; i < NBITARRAY; i++) {
			unsigned int word = atomic_load(&ba[i]);
			int bit = __builtin_ffs(word);
			cid = CID(i, bit);
			if (bit != 0 && cid != c->cid) {
				break;
			}
		}
		if (cid == -1) {
			c->npreempt_find_fail++;
			return cid;
		}
		ok = preemptable_clear(ba, cid, c);
		if (ok) {
			c->npreempt_find_ok++;
		} else {
			cid = -1;
			c->npreempt_retry++;
		}
	}
	return cid;
}

