#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include "core.h"
#include "util.h"

#define N 200

char buf[32];

struct log_entry *ring; 

#define IDX(idx) ((idx) % N)

void print(struct log_entry *r, int idx) {
	for(int i = idx; IDX(i+1) != idx; i = IDX(i+1)) {
		printf("%d: ts %ld vt %d\n", i, ring[i].ts, ring[i].vt);
	}
}

int rank_error(struct log_entry *ring, int idx) {
	int re = 0;
	for(int i = idx; IDX(i+1) != idx; i = IDX(i+1)) {
		if(ring[idx].vt > ring[i].vt) {
			// printf("re: idx %d %d i %d %d\n", idx, ring[idx].vt, i, ring[i].vt);
			re += 1; 
		}
	}
	return re;
}

void main(int argc, char *argv[]) {
	if (argc != 2) {
		printf("%s <logfile>.log\n", argv[0]);
		exit(1);
	}
	sprintf(buf, "/tmp/%s.log", argv[1]);
	int fd = open(buf, O_RDONLY);
	if(fd < 0) {
		perror("open");
		exit(1);
	}
	ring = malloc(sizeof(struct log_entry)* N);
	if(read(fd, ring, sizeof(struct log_entry) * N) <= 0) {
		perror("init ring read");
		exit(1);
	}
	// print(ring, 0);
	int idx = 0;
	int sum_re = 0;
	int nentry = 0;
	int max_re = 0;
	long max_ts;
	int max_vt;
	while(1) {
		int re = rank_error(ring, idx);
		// if(re > 0) printf("%d: %ld rank_error %d\n", idx, ring[idx].ts, re);
		if(re > max_re) {
			max_re = re;
			max_ts = ring[idx].ts;
			max_vt = ring[idx].vt;
		}
		sum_re += re;
		int n = read(fd, ring+idx, sizeof(struct log_entry));
		if (n < 0) {
			perror("next ring read");
			exit(1);
		}
		// printf("%d: read ts %ld vt %d\n", idx, ring[idx].ts, ring[idx].vt);
		if (n == 0)
			break;
		idx = IDX(idx + 1);
		nentry += 1;
	}
	printf("sum_re %d n %d %0.2f max %d (%ld, %d)\n", sum_re, nentry, AVG(sum_re, nentry), max_re, max_ts, max_vt);
}
