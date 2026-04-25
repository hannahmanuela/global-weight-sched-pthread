#include "assert.h"

#include "core.h"
#include "mcounter.h"

#define NC 4 
#define GRP1 0

int num_cores = NC;
bool do_affinity = false;

int main() {
	struct mcntr *mc = mc_new();
	struct core *c[NC] = {c_new(0, GRP1, 0)};

	assert(mc_is_zero(mc, c[0]));
	mc_inc(mc, c[0]);
	assert(!mc_is_zero(mc, c[0]));
}
