#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include "core.h"
#include "util.h"

// run: ./rankerror vtlog

#define N 200
#define Hz (3000 * 10L) // cycles per us * usec

char buf[32];

#define NBIN 50
#define NBIN_DELAY 100
#define NBIN_LAT 1000

int bin_rank_error[NBIN];
int bin_delay_error[NBIN_DELAY];
int bin_latency[NBIN_LAT];

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

int rank_error(struct log_entry *ring, long idx) {
	int re = 0;
	for(int i = idx+1; i < idx+N; i++) {
		if(weight > 0 && (ring[IDX(i)].w != weight))
			continue;
		if(ring[IDX(i)].vt < ring[IDX(idx)].vt) {
			re += 1; 
			if(re >=  N-1) {
				printf("re: idx %d %ld i %d %ld\n", idx, ring[IDX(idx)].vt, i, ring[IDX(i)].vt);
				// print(ring, idx);
			}
		}
	}
	return re;
}

int delay(struct log_entry *ring, long idx) {
	int d = 0;
	for(long i = idx-1; i > idx-N; i--) {
		if(weight > 0 && (ring[IDX(i)].w != weight))
			continue;
		if(ring[IDX(i)].vt > ring[IDX(idx)].vt) {
			d += 1; 
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
	long sum_d = 0;

	long max_re = 0;
	long max_re_idx;
	t_t max_re_ts;
	vt_t max_re_vt;

	long max_d = 0;
	long max_d_idx;
	t_t max_d_ts;
	vt_t max_d_vt;
	
	vt_t max_lat = 0;
	vt_t sum_lat = 0;

	if(read(fd, ring, sizeof(struct log_entry) * N) <= 0) {
		perror("init ring read");
		exit(1);
	}

	while(1) {
		if((weight == 0) || (ring[IDX(idx)].w == weight)) {
			nentry += 1;

			int re = rank_error(ring, idx);
			if(re > 0) {
				// printf("%d: %ld rank_error %d\n", idx, ring[idx].ts, re);
			}
			if(re > max_re) {
				max_re = re;
				max_re_idx = idx;
				max_re_ts = ring[IDX(idx)].ts;
				max_re_vt = ring[IDX(idx)].vt;
			}
			sum_re += re;
			bin_rank_error[(re%NBIN)]++;

			int d = delay(ring, idx+N-1);
			if(d > 0) {
				// printf("%d: %ld delay %d\n", idx, ring[idx].ts, d);
			}
			if(d > max_d) {
				max_d = d;
				max_d_idx = idx;
				max_d_ts = ring[IDX(idx)].ts;
				max_d_vt = ring[IDX(idx)].vt;
			}
			sum_d += d;
			bin_delay_error[(d%NBIN_DELAY)]++;

			// only makes sense if ts and vt are comparable, as in RR
			t_t lat = ring[IDX(idx)].ts - ring[IDX(idx)].vt;
			sum_lat += lat;
			if (lat/Hz > NBIN_LAT) {
				printf("adjust Hz or NBIN_LAT %d %d\n", lat/Hz, NBIN_LAT);
				vt_t diff = ring[IDX(idx)].ts-ring[IDX(idx)].vt;
				printf("lat: idx %d insert %ld sched %ld = %ld cycles\n", idx, ring[IDX(idx)].vt, ring[IDX(idx)].ts, diff);
			} else {
				bin_latency[(lat / Hz)]++;
			}
			if(lat > max_lat) {
				max_lat = lat;
			}
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
	printf("sum_re %d n %d %0.2f max %d (idx %ld ts %lld, vt %lld) weight %d\n", sum_re, nentry, AVG(sum_re, nentry), max_re, max_re_idx, max_re_ts, max_re_vt, weight);
	printf("distribution of rank errors:\n");
	for(int i = 0; i < NBIN; i++)
		if (bin_rank_error[i] > 0) printf("  bin %d: %d\n", i, bin_rank_error[i]);
	printf("=\n");
	printf("sum_d %d n %d %0.2f max %d (idx %ld ts %ld, vt %lld)\n", sum_d, nentry, AVG(sum_d, nentry), max_d, max_d_idx, max_d_ts, max_d_vt);
	printf("distribution of delay errors\n");
	for(int i = 0; i < NBIN_DELAY; i++)
		if (bin_delay_error[i] > 0) printf("  bin %d: %d\n", i, bin_delay_error[i]);
	printf("=\n");

	printf("distribution of latency errors (bin is %ld cycles) avg %ld max %ld\n", Hz, sum_lat/nentry, max_lat);
	for(int i = 0; i < NBIN_LAT; i++)
		if (bin_latency[i] > 0) printf("  bin %d: %d\n", i, bin_latency[i]);
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
