#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <strings.h>
#include <unistd.h>
#include <immintrin.h>

#include "core.h"
#include "util.h"
#include "group.h"
#include "mheap.h"

extern bool do_affinity;

void core_print(struct core *c) {
	struct process *p = c->process;
	if (p == NULL) {
		printf("  pid %d ", -1);
	} else {
		printf("  [pid %d vt %d w %d]", p->pid, p->he.vruntime, p->he.weight);
	}
}

void c_print(struct core *c, int num_groups) {
#if 0
	printf("    c %d: ", c->cid);
	printf(" us(cycles): sched %ld %0.2f enq %ld %0.2f deq %ld %0.2f yield %ld %0.2f",
	       c->cid,
	       c->nsched, AVG(c->sched_cycles, c->nsched),
	       c->nenq, AVG(c->enq_cycles, c->nenq),
	       c->ndeq, AVG(c->deq_cycles, c->ndeq),
	       c->nyield, AVG(c->yield_cycles, c->nyield));
#endif
	if(do_affinity) {
		for (int j = 0; j < num_groups; j++) {
			printf("[gid %d: h %d m %d %0.2f] ", j, c->hit[j], c->miss[j],
			       AVG(c->hit[j], (c->hit[j]+c->miss[j])));
		}
		printf("\n");
	}
}

int c_rand(struct core *c, int n) {
	int r = rand_r(&c->seed) % n;
	return r;
}

struct core *c_new(int i, int n, int seed) {
	struct core *c = (struct core *) malloc(sizeof(struct core));
	bzero(c, sizeof(struct core));
	c->cid = i;
	c->seed = seed;
	c->hit = calloc(n, sizeof(int));
	c->miss = calloc(n, sizeof(int));
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

void c_log_append(struct core *c, struct process *p) {
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
	c->log[i].vt = p->he.vruntime;
	c->log[i].cid = c->cid;
	c->log[i].pid = p->pid;
	c->log[i].gid = p->group->gid;
	c->log[i].w = p->he.weight;
	c->log[i].hid = p->h->id;
	c->log[i].ohid = p->other_hid;
	c->log[i].ovt = p->other_vt;
}

void c_log_done(struct core *c) {
	if(c->fd > 0) {
		if (write(c->fd, c->log, sizeof(struct log_entry) * c->log_nentry) < 0) {
			exit(1);
		}
		close(c->fd);
	}
}
