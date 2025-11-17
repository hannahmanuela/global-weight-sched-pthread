#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#include "core.h"

struct log_entry *logs;
int *fds;
char buf[32];

void main(int argc, char *argv[]) {
	if (argc != 3) {
		printf("%s: <n> <name>");
		exit(1);
	}
		
	int n = atoi(argv[1]);
	fds = malloc(n * sizeof(int));
	logs = malloc(n * sizeof(struct log_entry));

	sprintf(buf, "/tmp/%s.log", argv[2]);
	int fd = open(buf, O_CREAT|O_TRUNC|O_WRONLY, S_IRUSR|S_IWUSR);
	
	long ts = 0;
	int idx = 0;
	for (int i = 0; i < n; i++) {
		sprintf(buf, "/tmp/%s-%d.log", argv[2], i);
		fds[i] = open(buf, O_RDONLY);
		int r = read(fds[i], logs+i, sizeof(struct log_entry));
		if( r < 0) {
			perror("read");
			exit(1);
		}
		if(ts == 0 || ts > logs[i].ts) {
			ts = logs[i].ts;
			idx = i;
		}
	}
	while(1) {
		printf("%d ts %ld\n", idx, ts);
		dprintf(fd, "%ld %d\n", ts, logs[idx].vt);
		int r = read(fds[idx], logs+idx, sizeof(struct log_entry));
		if( r < 0) {
			perror("read");
			exit(1);
		}
		if(r == 0) exit(0);
		ts = 0;
		for (int i = 0; i < n; i++) {
			if (ts == 0 || ts > logs[i].ts) {
				ts = logs[i].ts;
				idx = i;
			}
		}
	}
}
