#ifndef _CORE_H_

#define _CORE_H_

#include "ticks.h"

struct core {
	int cid;
	unsigned int seed;
	struct drand48_data *buf;
	struct tick work;
	struct tick idle;
	struct tick total;
	struct process *current_process;
	struct process *pool;
	long sched_cycles;
	long min_proc_cycles;
	long enq_cycles;
	long deq_cycles;
	long yield_cycles;
	long nsched;
	long nsched_null;
	long nenq;
	long ndeq;
	long nyield;
	long nretry_del;
	long nretry_del_lock;
	long nretry_ins;
	long hit;
} __attribute__((aligned(64)));

void c_print(struct core *c);
int c_rand(struct core *c, int n);
struct core *c_new(int i);

#endif
