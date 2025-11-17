#ifndef _UTIL_H_

#define _UTIL_H_

#include <stdint.h>

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) >= (b)) ? (a) : (b))

#define AVG(s, n) ((1.0 * (s))/(n))

#define CACHE_LINE_SZ 64

#include "vt.h"

void error(char *);
double now();
long safe_read_tsc();

uint64_t perf_read_l2(int fd);
int perf_config(int cid);

#endif
