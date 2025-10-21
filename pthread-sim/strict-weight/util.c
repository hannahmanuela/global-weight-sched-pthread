#include <immintrin.h>

long safe_read_tsc() {
	_mm_lfence();
	long ret_val = _rdtsc();
	_mm_lfence();
	return ret_val;
}

