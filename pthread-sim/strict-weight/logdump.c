#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include "core.h"
#include "util.h"

// run ./log vtlog

#define N 100
char buf[32];

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
	struct log_entry *log = malloc(sizeof(struct log_entry)* N);
	long ts = 0;
	while(1) {
		int n = read(fd, log, sizeof(struct log_entry) * N);
		if (n < 0) {
			perror("init ring read");
			exit(1);
		}
		if (n == 0) break;
		for (int i = 0; i < N; i++) {
			if (ts > log[i].ts) {
				printf("not sorted %d %ld\n", i, log[i].ts);
				exit(1);
			}
			ts = log[i].ts;
			printf("ts %ld vt %d cid %d pid %d(%d, %d) hid %d ohid %d ovt %ld\n", log[i].ts, log[i].vt, log[i].cid, log[i].pid, log[i].gid, log[i].w, log[i].hid, log[i].ohid, log[i].ovt);
		}
	}
	close(fd);
}
