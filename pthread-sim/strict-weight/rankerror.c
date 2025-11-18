#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include "core.h"

#define N 100

char buf[32];

struct log_entry *ring; 

#define IDX(idx) ((idx+1) % N)

void print(struct log_entry *r, int idx) {
	for(int i = idx; IDX(i+1) != idx; i = IDX(i)) {
		printf("%d: ts %ld vt %d\n", i, ring[i].ts, ring[i].vt);
	}
}

int rank_error(struct log_entry *ring, int idx) {
	int re = 0;
	for(int i = idx; IDX(i+1) != idx; i = IDX(i)) {
		if(ring[idx].vt > ring[i].vt) {
			// printf("re: %d %d %d %d %d\n", idx, ring[idx].vt, i, ring[i].vt);
			re += 1; 
		}
	}
	return re;
}

void main(int argc, char *argv[]) {
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
	while(1) {
		int re = rank_error(ring, idx);
		printf("%d: %ld rank_error %d\n", idx, ring[idx].ts, re);
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
	printf("sum_re %d n %d\n", sum_re, nentry);
}
