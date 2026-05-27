
#include <stdio.h>
#include <stdatomic.h>

#include "preempt.h"
#include "core.h"

//
// XXX implement with new reduction instructions (AOR)?
//

#define CID(i, bit)  ((i) * (sizeof(unsigned long) * 8) + bit - 1)
#define BAINDEX(cid) ((cid) / 8)
#define BAOFFSET(cid) ((cid) % sizeof(unsigned long))

// only current core should call preemptable_set for itself
bool preemptable_set(bitarray_t ba, int cid) {
	unsigned long r = atomic_fetch_or(&ba[0], (1 << cid));
	bool set = r & (1 << cid);
	if (!set) {
		mycore()->npreempt_set++;
	}
	return !set;
}

bool preemptable_clear(bitarray_t ba, int cid) {
	unsigned long mask = ~(1U << cid);
	unsigned long r = atomic_fetch_and(&ba[0], mask);
	bool ok = r & (1 << cid);
	if(ok) {
		mycore()->npreempt_clear++;
	}
	return ok;
}

int preemptable_find_and_clear(bitarray_t ba) {
	int cid = -1;
	bool ok = false;
	while (!ok) {
		for (int i = 0; i < NBITARRAY; i++) {
			unsigned long word = atomic_load(&ba[i]);
			int bit = __builtin_ffsl(word);
			cid = CID(i, bit);
			if (bit != 0 && cid != mycore()->cid) {
				break;
			}
		}
		if (cid == -1) {
			mycore()->npreempt_find_fail++;
			return cid;
		}
		ok = preemptable_clear(ba, cid);
		if (ok) {
			mycore()->npreempt_find_ok++;
		} else {
			cid = -1;
			mycore()->npreempt_retry++;
		}
	}
	return cid;
}

