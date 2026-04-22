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
int bin[NBIN];

struct log_entry *ring; 

int weight = 0;

#define IDX(idx) ((idx) % N)

void print(struct log_entry *r, int idx) {
	for(int i = idx; IDX(i+1) != idx; i = IDX(i+1)) {
		printf("%d: ts %ld vt %lld gtt %lld cid %d pid %d(%d) hid %d ohid %d ovt %lld\n", i, ring[i].ts, ring[i].vt, ring[i].gtt, ring[i].cid, ring[i].pid, ring[i].gid, ring[i].hid, ring[i].ohid, ring[i].ovt);
	}
}

int rank_error(struct log_entry *ring, int idx, vt_t *maxdiff) {
	int re = 0;
	int using_gtt = (ring[idx].gtt > 0);
	for(int i = idx; IDX(i+1) != idx; i = IDX(i+1)) {
		if(weight > 0 && (ring[idx].w != weight))
			continue;
		if ((using_gtt && ring[idx].gtt > ring[i].gtt) || 
			(ring[idx].vt > ring[i].vt)) {
			re += 1; 
			vt_t d = ring[idx].vt-ring[i].vt;
			if(d > *maxdiff) *maxdiff = d;
				
#if 0
			if(re == 1) {
			  printf("re: idx %d %ld i %d %ld\n", idx, ring[idx].vt, i, ring[i].vt);
			  
			  // print(ring, idx);
			}
#endif
		}
	}
	return re;
}


void process_log(int fd) {
	// print(ring, 0);
	int idx = 0;
	int sum_re = 0;
	int nentry = 0;
	int max_re = 0;
	long max_ts;
	vt_t max_vt;
	vt_t max_diff;

	if(read(fd, ring, sizeof(struct log_entry) * N) <= 0) {
		perror("init ring read");
		exit(1);
	}

	while(1) {
		if((weight == 0) || (ring[idx].w == weight)) {
			nentry += 1;
			int re = rank_error(ring, idx, &max_diff);
			// if(re > 0) printf("%d: %ld rank_error %d\n", idx, ring[idx].ts, re);
			if(re > max_re) {
				max_re = re;
				max_ts = ring[idx].ts;
				max_vt = ring[idx].vt;
			}
			sum_re += re;
			bin[(re%NBIN)]++;
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
	printf("sum_re %d n %d %0.2f max %d (ts %ld, vt %lld, diff %lld) weight %d\n", sum_re, nentry, AVG(sum_re, nentry), max_re, max_ts, max_vt, max_diff, weight);
	printf("distribution of rank errors:\n");
	for(int i = 0; i < NBIN; i++)
		if (bin[i] > 0) printf("  re %d: %d\n", i, bin[i]);
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
