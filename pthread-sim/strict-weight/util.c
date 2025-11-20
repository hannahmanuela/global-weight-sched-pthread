#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <pthread.h>

#include <sys/time.h>
#include <sys/ioctl.h>
#include <immintrin.h>
#include <linux/perf_event.h>
#include <asm/unistd.h>

#include "vt.h"

long safe_read_tsc() {
	_mm_lfence();
	long ret_val = _rdtsc();
	_mm_lfence();
	return ret_val;
}

void error(char *s) {
	fprintf(stderr, "error: %s\n", s);
	assert(0);
}

double now()
{
	struct timeval tv;
	gettimeofday(&tv, 0);
	return tv.tv_sec + tv.tv_usec / 1000000.0;
}
