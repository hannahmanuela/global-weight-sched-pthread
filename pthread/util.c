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

extern pthread_key_t core_key;

struct core *get_core() {
	struct core **c = pthread_getspecific(core_key);
	assert(c != NULL);
	return *c;
}

long safe_read_tsc() {
	unsigned int aux;
	long ret_val = _rdtscp(&aux);
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
