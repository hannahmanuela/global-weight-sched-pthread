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

FILE *log_fd;

pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

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

int log_init(char *name) {
	log_fd = fopen(name, "w");
	if (log_fd == NULL)
		return -1;
	else
		return 0;
}

void log_vt(int cid, vt_t t) {
	pthread_mutex_lock(&log_mutex);
	fprintf(log_fd, "%d: vruntime %d cid %d\n", safe_read_tsc(), t, cid);
	pthread_mutex_unlock(&log_mutex);
}

static long
perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
                int cpu, int group_fd, unsigned long flags)
{
    int ret;

    ret = syscall(__NR_perf_event_open, hw_event, pid, cpu,
                   group_fd, flags);
    return ret;
}

int perf_config(int cid) {
	struct perf_event_attr pe;
	int fd;

	memset(&pe, 0, sizeof(struct perf_event_attr));
	pe.size = sizeof(struct perf_event_attr);
	// https://www.intel.com/content/dam/develop/external/us/en/documents/performance-analysis-guide-181827.pdf
	pe.type = PERF_TYPE_HW_CACHE;
	// pe.config = PERF_COUNT_HW_CACHE_L2 | PERF_COUNT_HW_CACHE_OP_READ << 8 | PERF_COUNT_HW_CACHE_RESULT_MISS << 16;
	pe.disabled = 1; // Start disabled
	pe.exclude_kernel = 1; // Exclude kernel events
	pe.exclude_hv = 1; // Exclude hypervisor events

    
	fd = perf_event_open(&pe, 0, cid, -1, 0);
	if (fd < 0) {
		fprintf(stderr, "Error opening perf event on core %d: %s\n", cid, strerror(errno));
		return -1;
	}
	ioctl(fd, PERF_EVENT_IOC_RESET, 0);
	ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
	return fd;
}

uint64_t perf_read_l2(int fd) {
	uint64_t count = 0;;
	ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);

	if (read(fd, &count, sizeof(uint64_t)) < 0) {
		perror("read");
	}
	close(fd);
	return count;
}
