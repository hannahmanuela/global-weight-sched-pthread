#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <pthread.h>

#include <sys/time.h>
#include <time.h>
#include <sys/ioctl.h>
#include <immintrin.h>
#include <linux/perf_event.h>
#include <asm/unistd.h>

#include "vt.h"
#include "util.h"

static vt_t init_tsc __calign__;  // set once at beginning of time

double tsc_per_us;  // calibrated TSC cycles per microsecond

void tsc_init() {
	init_tsc = safe_read_tsc();

	// calibrate the (invariant) TSC rate against CLOCK_MONOTONIC over ~10ms
	struct timespec t0, t1;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	long c0 = safe_read_tsc();
	long ns;
	do {
		clock_gettime(CLOCK_MONOTONIC, &t1);
		ns = (t1.tv_sec - t0.tv_sec) * 1000000000L + (t1.tv_nsec - t0.tv_nsec);
	} while (ns < 10 * 1000 * 1000);
	long c1 = safe_read_tsc();
	tsc_per_us = (double)(c1 - c0) / (ns / 1000.0);
}

// burn ~us microseconds doing computation (busy-wait on the TSC) instead of
// sleeping. usleep(1) actually costs ~50us on Linux; this hits ~1us.
void work_us(long us) {
	long target = safe_read_tsc() + (long)(us * tsc_per_us);
	while (safe_read_tsc() < target)
		_mm_pause();
}

long safe_read_tsc() {
	unsigned int aux;
	long ret_val = _rdtscp(&aux);
	return ret_val;
}

long tsc_now() {
	return safe_read_tsc() - init_tsc;
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
