#ifndef _CORE_H_

#define _CORE_H_

#include <stdatomic.h>

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
	long nsched;
	long nsched_null;

	long min_proc_cycles;

	long enq_cycles;
	long nenq;

	long deq_cycles;
	long ndeq;

	long yield_cycles;
	long nyield;

	
	long nretry_del;
	long nretry_del_lock;
	long nretry_ins;

	long hit;

	long wait_for_wr_heap_lock_cycles;
	long num_times_wr_heap_locked;
	atomic_long wait_for_rd_heap_lock_cycles;
	atomic_long num_times_rd_heap_locked;
	long insert_cycles;
	long remove_cycles;
	long ninsert;
	long nremove;	
} __attribute__((aligned(64)));

void c_print(struct core *c);
int c_rand(struct core *c, int n);
struct core *c_new(int i);

#endif
