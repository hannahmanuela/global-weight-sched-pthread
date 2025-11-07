#include <stdio.h>
#include <immintrin.h>

long safe_read_tsc() {
	_mm_lfence();
	long ret_val = _rdtsc();
	_mm_lfence();
	return ret_val;
}

void error(char *s) {
	fprintf(stderr, "error: %s\n", s);
	exit(1);
}

