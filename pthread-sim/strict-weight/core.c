#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <strings.h>
#include <unistd.h>
#include <immintrin.h>

#include "core.h"
#include "util.h"

void c_print(struct core *c) {
	printf("%d: us(cycles): sched %ld %0.2f enq %ld %0.2f deq %ld %0.2f yield %ld %0.2f",
	       c->cid,
	       c->nsched, AVG(c->sched_cycles, c->nsched),
	       c->nenq, AVG(c->enq_cycles, c->nenq),
	       c->ndeq, AVG(c->deq_cycles, c->ndeq),
	       c->nyield, AVG(c->yield_cycles, c->nyield));
}

int c_rand(struct core *c, int n) {
	int r;

	// double rand;
	// drand48_r(c->buf, &rand);
	// r = (int) (rand * n);

	r = rand_r(&c->seed) % n;
	return r;
}

struct core *c_new(int i) {
	struct core *c = (struct core *) malloc(sizeof(struct core));
	bzero(c, sizeof(struct core));
	c->cid = i;
	// c->seed = getpid() + i;
	c->seed = i;

	c->buf = malloc(sizeof(struct drand48_data));
	srand48_r(i, c->buf);

	return c;
}

void c_log_init(struct core *c, char *name) {
	char buf[32] = {'\0'};
	sprintf(buf, "%s-%d.log", name, c->cid);
	c->fd = open(buf, O_CREAT|O_TRUNC|O_WRONLY, S_IRUSR|S_IWUSR); 
	if(c->fd <= 0) {
		perror("c_log_init: open");
		exit(1);
	}
}

void c_log_append(struct core *c, vt_t vt) {
	if(c->log_nentry == 0) {
		c->log = malloc(sizeof(struct log_entry) * LOG_NENTRY);
	}
	if(c->log_nentry == LOG_NENTRY) {
		int n = write(c->fd, c->log, sizeof(struct log_entry) * LOG_NENTRY);
		if (n <= 0) {
			perror("c_log_append: write");
			exit(1);
		}
		// printf("%d: ts %ld vt %d\n", c->cid, c->log[0].ts, c->log[0].vt);
		c->log_nentry = 0;
	}
	int i = c->log_nentry++;
	c->log[i].ts = _rdtsc();
	c->log[i].vt = vt;
}

void c_log_done(struct core *c) {
	if(c->fd > 0) {
		if (write(c->fd, c->log, sizeof(struct log_entry) * c->log_nentry) < 0) {
			exit(1);
		}
		close(c->fd);
	}
}
