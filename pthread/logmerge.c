#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#include "core.h"

// run: ./logmerge 4 vtlog

struct log_entry *logs;
int *fds;
char buf[32];

void main(int argc, char *argv[]) {
	if (argc != 3) {
		printf("%s: <n> <name>", argv[0]);
		exit(1);
	}
		
	int n = atoi(argv[1]);
	fds = malloc(n * sizeof(int));
	logs = malloc(n * sizeof(struct log_entry));

	sprintf(buf, "/tmp/%s.log", argv[2]);
	int fd = open(buf, O_CREAT|O_TRUNC|O_WRONLY, S_IRUSR|S_IWUSR);
	
	// load first entry on each core
	for (int i = 0; i < n; i++) {
		sprintf(buf, "/tmp/%s-%d.log", argv[2], i);
		fds[i] = open(buf, O_RDONLY);
		int r = read(fds[i], logs+i, sizeof(struct log_entry));
		if(r < 0) {
			perror("read");
			exit(1);
		}
	}
	// merge per-core logs into one log ordered by ts
	while(1) {
		long ts = 0;
		int idx;
		for (int i = 0; i < n; i++) {
			if (ts == 0 || ts > logs[i].ts) {
				ts = logs[i].ts;
				idx = i;
			}
		}
		// printf("%d: smallest %ld\n", idx, ts);
		if (write(fd, logs+idx, sizeof(struct log_entry)) <= 0) {
			perror("write");
			exit(1);
		}
		int r = read(fds[idx], logs+idx, sizeof(struct log_entry));
		if(r < 0) {
			perror("read");
			exit(1);
		}
		if(r == 0)
			break;
		assert(logs[idx].ts >= ts);
	}
	for(int i = 0; i < n; i++)
		close(fds[i]);
}
