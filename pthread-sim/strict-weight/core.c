#include <stdlib.h>
#include <stdio.h>
#include <strings.h>

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
	double rand;
	int r;

	drand48_r(c->buf, &rand);
	r = (int) (rand * n);
	return r;
}

struct core *c_new(int i) {
	struct core *c = (struct core *) malloc(sizeof(struct core));
	bzero(c, sizeof(struct core));
	c->cid = i;
	c->buf = malloc(sizeof(struct drand48_data));
	srand48_r(i, c->buf);

	return c;
}
