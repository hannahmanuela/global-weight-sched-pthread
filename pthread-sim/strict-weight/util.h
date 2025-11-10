#ifndef _UTIL_H_

#define _UTIL_H_

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) >= (b)) ? (a) : (b))

#define AVG(s, n) ((1.0 * (s))/(n))

#define CACHE_LINE_SZ 64

void error(char *);
double now();

#include <immintrin.h>

inline long safe_read_tsc() {
	_mm_lfence();
	long ret_val = _rdtsc();
	_mm_lfence();
	return ret_val;
}

#endif
