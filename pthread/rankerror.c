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
	for(int i = idx; i < idx+N; i++) {
		int j = IDX(i);
		printf("%d: ts %ld vt %lld cid %d pid %d(%d) hid %d ohid %d ovt %lld\n", i, ring[j].ts, ring[j].vt, ring[j].cid, ring[j].pid, ring[j].gid, ring[j].hid, ring[j].ohid, ring[j].ovt);
	}
}

void print_back(struct log_entry *r, int idx) {
	for(int i = idx; i > idx-N; i--) {
		int j = IDX(i);
		printf("%d: ts %ld vt %lld cid %d pid %d(%d) hid %d ohid %d ovt %lld\n", i, ring[j].ts, ring[j].vt, ring[j].cid, ring[j].pid, ring[j].gid, ring[j].hid, ring[j].ohid, ring[j].ovt);
	}
}

int rank_error(struct log_entry *ring, long idx, vt_t *maxdiff, int *j) {
	int re = 0;
	for(int i = idx+1; i < idx+N; i++) {
		if(weight > 0 && (ring[IDX(i)].w != weight))
			continue;
		if(ring[IDX(i)].vt < ring[IDX(idx)].vt) {
			re += 1; 
			vt_t dt = ring[IDX(idx)].vt - ring[IDX(i)].vt;
			if(dt > *maxdiff) {
				*maxdiff = dt;
				*j = i;
			}
			if(re >=  N-1) {
				printf("re: idx %d %ld i %d %ld\n", idx, ring[IDX(idx)].vt, i, ring[IDX(i)].vt);
				// print(ring, idx);
			}
		}
	}
	return re;
}

int delay(struct log_entry *ring, long idx, vt_t *maxdiff, int *j) {
	int d = 0;
	for(long i = idx-1; i > idx-N; i--) {
		if(weight > 0 && (ring[IDX(i)].w != weight))
			continue;
		if(ring[IDX(i)].vt > ring[IDX(idx)].vt) {
			d += 1; 
			vt_t dt = ring[IDX(i)].vt-ring[IDX(idx)].vt;
			if(dt > *maxdiff) {
				*maxdiff = dt;
				*j = i;
			}
			if(d >= N-1) {
				printf("delay: idx %d %ld i %d %ld\n", idx, ring[IDX(idx)].vt, i, ring[IDX(i)].vt);
				// print_back(ring, idx);
			}
		
		}
	}
	return d;
}


void process_log(int fd) {
	// print(ring, 0);
	long idx = 0;

	long sum_re = 0;
	int nentry = 0;
	int max_re = 0;
	vt_t max_ts;
	vt_t max_vt;
	long max_idx;

	vt_t max_re_diff = 0;
	vt_t max_re_diff_ts;
	vt_t max_re_diff_vt;
	long max_re_diff_idx;
	long max_re_diff_i;
	
	long sum_d = 0;
	int max_d = 0;
	vt_t max_d_ts;
	vt_t max_d_vt;
	long max_d_idx;

	vt_t max_d_diff = 0;
	vt_t max_d_diff_ts;
	vt_t max_d_diff_vt;
	long max_d_diff_idx;
	long max_d_diff_i;

	if(read(fd, ring, sizeof(struct log_entry) * N) <= 0) {
		perror("init ring read");
		exit(1);
	}

	while(1) {
		if((weight == 0) || (ring[IDX(idx)].w == weight)) {
			nentry += 1;
			vt_t max_re_vt = 0;
			int max_re_i;
			int i = 0;
			int re = rank_error(ring, idx, &max_re_vt, &i);
			if(re > 0) {
				// printf("%d: %ld rank_error %d\n", idx, ring[idx].ts, re);
			}
			if(re > max_re) {
				max_re = re;
				max_idx = idx;
				max_ts = ring[IDX(idx)].ts;
				max_vt = ring[IDX(idx)].vt;
			}
			if (max_re_vt > max_re_diff) {
				max_re_diff = max_re_vt;
				max_re_diff_idx = idx;
				max_re_diff_ts = ring[IDX(idx)].ts;
				max_re_diff_vt = ring[IDX(idx)].vt;
				max_re_diff_i = i;
				
			}
			sum_re += re;
			bin_re[(re%NBIN)]++;

			vt_t max_vt_d = 0;
			i = 0;
			int d = delay(ring, idx+N-1, &max_vt_d, &i);
			if(d > 0) {
				// printf("%d: %ld delay %d\n", idx, ring[idx].ts, d);
			}
			if(d > max_d) {
				max_d = d;
				max_d_idx = idx;
				max_d_ts = ring[IDX(idx)].ts;
				max_d_vt = ring[IDX(idx)].vt;
			}
			if (max_vt_d > max_d_diff) {
				max_d_diff = max_vt_d;
				max_d_diff_idx = idx;
				max_d_diff_ts = ring[IDX(idx)].ts;
				max_d_diff_vt = ring[IDX(idx)].vt;
				max_d_diff_i = i;
			}
					    
			sum_d += d;
			bin_d[(d%NBIN)]++;
		}
		int n = read(fd, ring+IDX(idx), sizeof(struct log_entry));
		if (n < 0) {
			perror("next ring read");
			exit(1);
		}
		// printf("%d: read ts %ld vt %d %d\n", idx, ring[idx].ts, ring[idx].vt, ring[idx].w);
		if (n == 0)
			break;
		idx++;
	}
	printf("sum_re %d n %d %0.2f max %d (idx %ld ts %lld, vt %lld) maxdiff %lld (idx %ld ts %ld vt %lld i %d) weight %d\n", sum_re, nentry, AVG(sum_re, nentry), max_re, max_idx, max_ts, max_vt, max_re_diff, max_re_diff_idx, max_re_diff_ts, max_re_diff_vt, max_re_diff_i, weight);
	printf("distribution of rank errors:\n");
	for(int i = 0; i < NBIN; i++)
		if (bin_re[i] > 0) printf("  bin %d: %d\n", i, bin_re[i]);
	printf("=\n");
	printf("sum_d %d n %d %0.2f max %d (idx %ld ts %ld, vt %lld) maxdiff %lld (idx %ld ts %ld vt %lld i %d) \n", sum_d, nentry, AVG(sum_d, nentry), max_d, max_d_idx, max_d_ts, max_d_vt, max_d_diff, max_d_diff_idx, max_d_diff_ts, max_d_diff_vt, max_d_diff_i);
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
