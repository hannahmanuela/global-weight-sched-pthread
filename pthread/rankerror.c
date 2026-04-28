#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include "core.h"
#include "util.h"

// run: ./rankerror vtlog

#define N 200

char buf[32];

#define NBIN 50
int bin_re[NBIN];
int bin_d[NBIN];

struct log_entry *ring; 

int weight = 0;

#define IDX(idx) ((idx) % N)

void print(struct log_entry *r, int idx) {
	for(int i = idx; IDX(i+1) != idx; i = IDX(i+1)) {
		printf("%d: ts %ld vt %lld cid %d pid %d(%d) hid %d ohid %d ovt %lld\n", i, ring[i].ts, ring[i].vt, ring[i].cid, ring[i].pid, ring[i].gid, ring[i].hid, ring[i].ohid, ring[i].ovt);
	}
}

int rank_error(struct log_entry *ring, int idx, vt_t *maxdiff) {
	int re = 0;
	for(int i = idx; IDX(i+1) != idx; i = IDX(i+1)) {
		if(weight > 0 && (ring[idx].w != weight))
			continue;
		if(ring[i].vt < ring[idx].vt) {
			re += 1; 
			vt_t d = ring[idx].vt-ring[i].vt;
			if(d > *maxdiff) *maxdiff = d;
			// printf("re: idx %d %ld i %d %ld\n", idx, ring[idx].vt, i, ring[i].vt);
			// print(ring, idx);
		
		}
	}
	return re;
}

int delay(struct log_entry *ring, int idx, vt_t *maxdiff) {
	int d = 0;
	for(int i = idx; i > idx-N; i--) {
		if(weight > 0 && (ring[IDX(idx)].w != weight))
			continue;
		if(ring[IDX(i)].vt > ring[IDX(idx)].vt) {
			d += 1; 
			vt_t d = ring[IDX(i)].vt-ring[IDX(idx)].vt;
			if(d > *maxdiff) *maxdiff = d;
			// printf("delay: idx %d %ld i %d %ld\n", idx, ring[IDX(idx)].vt, i, ring[IDX(i)].vt);
			// print(ring, idx);
		
		}
	}
	return d;
}


void process_log(int fd) {
	// print(ring, 0);
	int idx = 0;

	int sum_re = 0;
	int nentry = 0;
	int max_re = 0;
	long max_ts;
	vt_t max_vt;
	vt_t max_re_diff;

	int sum_d = 0;
	int max_d = 0;
	long max_d_ts;
	vt_t max_d_vt;
	vt_t max_d_diff;

	if(read(fd, ring, sizeof(struct log_entry) * N) <= 0) {
		perror("init ring read");
		exit(1);
	}

	while(1) {
		if((weight == 0) || (ring[idx].w == weight)) {
			nentry += 1;
			int re = rank_error(ring, idx, &max_re_diff);
			if(re > 0) {
				// printf("%d: %ld rank_error %d\n", idx, ring[idx].ts, re);
			}
			if(re > max_re) {
				max_re = re;
				max_ts = ring[idx].ts;
				max_vt = ring[idx].vt;
			}
			sum_re += re;
			bin_re[(re%NBIN)]++;

			int d = delay(ring, idx+N-1, &max_d_diff);
			if(d > 0) {
				// printf("%d: %ld delay %d\n", idx, ring[idx].ts, d);
			}
			if(d > max_d) {
				max_d = d;
				max_d_ts = ring[idx].ts;
				max_d_vt = ring[idx].vt;
			}
			sum_d += d;
			bin_d[(d%NBIN)]++;
		}
		int n = read(fd, ring+idx, sizeof(struct log_entry));
		if (n < 0) {
			perror("next ring read");
			exit(1);
		}
		// printf("%d: read ts %ld vt %d %d\n", idx, ring[idx].ts, ring[idx].vt, ring[idx].w);
		if (n == 0)
			break;
		idx = IDX(idx + 1);
	}
	printf("sum_re %d n %d %0.2f max %d (ts %ld, vt %lld, diff %lld) weight %d\n", sum_re, nentry, AVG(sum_re, nentry), max_re, max_ts, max_vt, max_re_diff, weight);
	printf("distribution of rank errors:\n");
	for(int i = 0; i < NBIN; i++)
		if (bin_re[i] > 0) printf("  bin %d: %d\n", i, bin_re[i]);
	printf("=\n");
	printf("sum_d %d n %d %0.2f max %d (ts %ld, vt %lld, diff %lld)\n", sum_d, nentry, AVG(sum_d, nentry), max_d, max_d_ts, max_d_vt, max_d_diff);
	printf("distribution of delay\n");
	for(int i = 0; i < NBIN; i++)
		if (bin_d[i] > 0) printf("  bin %d: %d\n", i, bin_d[i]);
	printf("=\n");
}

void main(int argc, char *argv[]) {
	int opt;
	
	while ((opt = getopt(argc, argv, "w:")) != -1) {
		switch(opt) {
		case 'w':
			weight = atoi(optarg);
			break;
		}
	}
	if (argc - optind != 1) {
		printf("%s <logfile>.log\n", argv[0]);
		exit(1);
	}
	sprintf(buf, "/tmp/%s.log", argv[optind]);
	int fd = open(buf, O_RDONLY);
	if(fd < 0) {
		perror("open");
		exit(1);
	}
	ring = malloc(sizeof(struct log_entry)* N);
	process_log(fd);
}
