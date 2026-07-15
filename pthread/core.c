#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <strings.h>
#include <unistd.h>
#include <pthread.h>
#include <immintrin.h>

#include "core.h"
#include "util.h"
#include "group.h"
#include "mheap.h"
#include "dllist.h"

extern bool do_affinity;

__thread struct core *tl_mycore;

void set_mycore(struct core *c) {
	tl_mycore = c;
}

struct core *mycore() {
	return tl_mycore;
}

// Build a pin order that puts one thread on every distinct physical core
// before using any hyperthread sibling. Discovered from sysfs at runtime,
// so it adapts to whatever machine we run on (no hardcoded topology).
#define MAX_CPUS 4096

static int pin_order[MAX_CPUS];
static int n_pins;
static pthread_once_t pin_once = PTHREAD_ONCE_INIT;

static int topo_read_int(int cpu, const char *field, int dflt) {
	char path[128];
	snprintf(path, sizeof(path),
	         "/sys/devices/system/cpu/cpu%d/topology/%s", cpu, field);
	FILE *f = fopen(path, "r");
	if (!f) return dflt;
	int v;
	if (fscanf(f, "%d", &v) != 1) v = dflt;
	fclose(f);
	return v;
}

static void build_pin_order(void) {
	int ncpu = sysconf(_SC_NPROCESSORS_ONLN);
	if (ncpu <= 0 || ncpu > MAX_CPUS) ncpu = 1;

	int pkg[MAX_CPUS], core[MAX_CPUS];
	for (int c = 0; c < ncpu; c++) {
		pkg[c]  = topo_read_int(c, "physical_package_id", 0);
		core[c] = topo_read_int(c, "core_id", c);
	}

	// tier t = the t-th hyperthread of each physical core. Emit all of tier 0
	// (one thread per core) before any of tier 1, so the first N_physical pins
	// never share a core.
	n_pins = 0;
	for (int tier = 0; n_pins < ncpu; tier++) {
		int added = 0;
		for (int c = 0; c < ncpu; c++) {
			int sib = 0;                 // siblings of c with a smaller cpu id
			for (int c2 = 0; c2 < c; c2++)
				if (pkg[c2] == pkg[c] && core[c2] == core[c]) sib++;
			if (sib == tier) { pin_order[n_pins++] = c; added++; }
		}
		if (!added) break;
	}
}

int calc_pin_cpu(int cid) {
	pthread_once(&pin_once, build_pin_order);
	return pin_order[cid % n_pins];
}

void core_print(struct core *c) {
	struct task_struct *p = c->process;
	if (p == NULL) {
		printf("  pid %d ", -1);
	} else {
		printf("  [pid %d vt %lld w %d]", p->pid, p->he.vruntime, p->he.weight);
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

int c_rand(int n) {
       double dr;
       struct core *c = mycore();
       drand48_r(&c->randBuffer, &dr);
       int r = (int) (dr * n);
       // int r = rand_r(&c->seed) % n;
       return r;
}

struct core *c_new(int i, int n, int seed) {
	struct core *c = (struct core *) malloc(sizeof(struct core));
	bzero(c, sizeof(struct core));
	lock_init(&c->lk);
	c->cid = i;
	c->preempted = NOHEAP;
	c->seed = seed;
	srand48_r(seed, &c->randBuffer);
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
	c->log = malloc(sizeof(struct log_entry) * LOG_NENTRY);
}

void c_log_append(struct task_struct *p) {
	struct core *c = mycore();
	if(c->log_nentry == LOG_NENTRY) {
		int n = write(c->fd, c->log, sizeof(struct log_entry) * LOG_NENTRY);
		if (n <= 0) {
			perror("c_log_append: write");
			exit(1);
		}
		c->log_nentry = 0;
	}
	int i = c->log_nentry++;
	c->log[i].ts_in = p->he.tsc_in;
	c->log[i].ts_out = p->he.tsc_out;
	c->log[i].cid = c->cid;
	c->log[i].pid = p->pid;
	c->log[i].gid = p->group->gid;
	c->log[i].vt = p->he.vruntime;
	c->log[i].w = p->he.weight;
	c->log[i].hid = p->he.id;
	if(c->log[i].ts_in > c->log[i].ts_out) {
		printf("%d: in %ld out %ld\n", c->cid, c->log[i].ts_in, c->log[i].ts_out);
		assert(0);
	}
}

void c_log_done(struct core *c) {
	if(c->fd > 0) {
		if (write(c->fd, c->log, sizeof(struct log_entry) * c->log_nentry) < 0) {
			exit(1);
		}
		close(c->fd);
	}
}

