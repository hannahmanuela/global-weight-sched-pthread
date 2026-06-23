#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include "core.h"
#include "util.h"
#include "rr.h"

// run: ./rankerror vtlog

#define N 200

char buf[32];

#define NBIN 50
#define NBIN_DELAY 100
#define NBIN_PRIORITY 100

int bin_rank_error[NBIN];
int bin_delay_error[NBIN_DELAY];
int bin_priority_error[NBIN_PRIORITY];

struct log_entry *ring; 

int weight = 0;
bool do_priority = false;

#define IDX(idx) ((idx) % N)
#define IN(i) ring[IDX(i)].ts_in
#define OUT(i) ring[IDX(i)].ts_out
#define VT(i) ring[IDX(i)].vt
#define CID(i) ring[IDX(i)].cid
#define PID(i) ring[IDX(i)].pid
#define HEAP(i) ring[IDX(i)].pid

void print(struct log_entry *r, int idx) {
	for(int i = idx; i < idx+N; i++) {
		int j = IDX(i);
		printf("%d: in %ld out %ld vt %lld cid %d pid %d(%d)\n", i, IN(i), OUT(i), VT(i), CID(i), ring[j].pid, ring[j].gid);
	}
}

void print_back(struct log_entry *r, int idx) {
	for(int i = idx; i > idx-N; i--) {
		int j = IDX(i);
		printf("%d: in %ld out %ld vt %lld cid %d pid %d(%d)\n", i, IN(i), OUT(i), VT(i), CID(i), ring[j].pid, ring[j].gid);
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
				printf("re: idx %d %ld i %d %ld\n", idx, VT(idx),  i, VT(i));
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



int priority(struct log_entry *ring, long idx) {
	int p = 0;
	if(ring[IDX(idx)].gid == RR_LOW) {  // skip low
		return -1;
	}
	for(long i = idx-1; i > idx-N; i--) {
		if(ring[IDX(i)].gid == RR_HIGH) {
			break;
		}
		// if idx was inserted before i, dequeued after i, and
		// vruntime idx is lower than i, the scheduler made an
		// error: idx was scheduled after i, even though it
		// could and should have run before i.
		if((IN(idx) < IN(i)) && (OUT(idx) > OUT(i)) && (VT(idx) < VT(i))) {
			p += 1; 
			printf("priority: idx %d p %d vt %ld h %d c %d i %d p %d vt %ld gid %d h %d c %d diff %ld\n", idx, PID(idx), VT(idx), HEAP(idx), CID(idx), i, PID(i), VT(i), ring[IDX(i)].gid, HEAP(i), CID(i), VT(idx)-VT(i));
			if(p >= N-1) {
				// print_back(ring, idx);
			}
		}
	}
	return p;
}


void process_log(int fd) {
	// print(ring, 0);
	long idx = 0;

	long sum_re = 0;
	int nentry = 0;
	long sum_d = 0;
	long sum_p = 0;

	long max_re = 0;
	long max_re_idx;
	t_t max_re_ts_in;
	t_t max_re_ts_out;

	long max_d = 0;
	long max_d_idx;
	t_t max_d_ts_in;
	t_t max_d_ts_out;
	
	long max_p = 0;
	long max_p_idx;
	t_t max_p_ts_in;
	t_t max_p_ts_out;
	t_t max_p_vt;
	
	vt_t max_lat = 0;
	vt_t sum_lat = 0;

	if(read(fd, ring, sizeof(struct log_entry) * N) <= 0) {
		perror("init ring read");
		exit(1);
	}

	while(1) {
		if((weight == 0) || (ring[IDX(idx)].w == weight)) {
			nentry += 1;

			if(!do_priority) {
				int re = rank_error(ring, idx);
				if(re > 0) {
					// printf("%d: %ld rank_error %d\n", idx, ring[idx].ts, re);
				}
				if(re > max_re) {
					max_re = re;
					max_re_idx = idx;
					max_re_ts_in = IN(idx);
					max_re_ts_out = OUT(idx);
				}
				sum_re += re;
				bin_rank_error[(re%NBIN)]++;

				int d = delay(ring, idx+N-1);
				if(d > 0) {
					// printf("%d: %ld delay %d\n", idx, ring[idx].ts, d);
				}
				if(d > max_d) {
					max_d = d;
					max_d_idx = idx + N - 1;
					max_d_ts_in = IN(max_d_idx);
					max_d_ts_out = OUT(max_d_idx);
				}
				sum_d += d;
				if(d < NBIN_DELAY)
					bin_delay_error[(d%NBIN_DELAY)]++;

			} else {
				int p = priority(ring, idx+N-1);
				if(p >= 0) {
					if(p > 0) {
						// printf("%d: %ld priority %d\n", idx, ring[idx].ts, p);
					}
					if(p > max_p) {
						max_p = p;
						max_p_idx = idx + N -1;
						max_p_ts_in = IN(max_p_idx);
						max_p_ts_out = OUT(max_p_idx);
						max_p_vt = VT(max_p_idx);
					}
					sum_p += p;
					if(p < NBIN_PRIORITY)
						bin_priority_error[(p%NBIN_PRIORITY)]++;
				}
			}

		}
		int n = read(fd, ring+IDX(idx), sizeof(struct log_entry));
		if (n < 0) {
			perror("next ring read");
			exit(1);
		}
		// printf("%d: read ts %ld vt %ld w %d gid %d\n", idx, ring[IDX(idx)].ts, ring[IDX(idx)].vt, ring[IDX(idx)].w, ring[IDX(idx)].gid);
		if (n == 0)
			break;
		idx++;
	}
	printf("sum_re %d n %d %0.2f max %d (idx %ld ts %lld, vt %lld, diff %lld) weight %d\n", sum_re, nentry, AVG(sum_re, nentry), max_re, max_re_idx, max_re_ts_in, max_re_ts_out, max_re_ts_out-max_re_ts_in, weight);
	printf("distribution of rank errors:\n");
	for(int i = 0; i < NBIN; i++)
		if (bin_rank_error[i] > 0) printf("  bin %d: %d\n", i, bin_rank_error[i]);
	printf("=\n");
	printf("sum_d %d n %d %0.2f max %d (idx %ld ts %ld, vt %lld, diff %lld)\n", sum_d, nentry, AVG(sum_d, nentry), max_d, max_d_idx, max_d_ts_in, max_d_ts_out, max_d_ts_out - max_d_ts_in);
	printf("distribution of delay errors\n");
	for(int i = 0; i < NBIN_DELAY; i++)
		if (bin_delay_error[i] > 0) printf("  bin %d: %d\n", i, bin_delay_error[i]);
	printf("=\n");
	if(do_priority) {
		printf("sum_p %d n %d %0.2f max %d (idx %ld ts_in %ld, ts_out %ld vt %lld, diff %lld)\n", sum_p, nentry, AVG(sum_p, nentry), max_p, max_p_idx, max_p_ts_in, max_p_ts_out, max_p_vt, max_p_ts_out - max_p_ts_in);
		printf("distribution of priority errors\n");
		for(int i = 0; i < NBIN_PRIORITY; i++)
			if (bin_priority_error[i] > 0) printf("  bin %d: %d\n", i, bin_priority_error[i]);
		printf("=\n");
	}
	
}

void main(int argc, char *argv[]) {
	int opt;
	
	while ((opt = getopt(argc, argv, "pw:")) != -1) {
		switch(opt) {
		case 'p':
			do_priority = true;
			break;
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
