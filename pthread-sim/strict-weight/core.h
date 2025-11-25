#ifndef _CORE_H_

#define _CORE_H_

#include <stdatomic.h>

#include "vt.h"
#include "util.h"

struct log_entry {
	long ts;
	vt_t vt;
	int cid;
	int pid;
	int gid;
	int hid;
	int ohid;
	vt_t ovt;
};

struct core {
	int cid;
	unsigned int seed;
	t_t work;
	t_t idle;
	t_t total;
	struct process *process;
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
	long max_retry_del;
	long max_retry_del_lock;
	long nrand;

	int *hit;

	struct log_entry *log;
	int log_nentry;
	int fd;

} __calign__;

#define LOG_NENTRY  1000000

void c_print(struct core *c);
int c_rand(struct core *c, int n);
struct core *c_new(int i, int n);
void c_log_init(struct core *c, char *name);
void c_log_append(struct core *c, struct process *p);
void c_log_done(struct core *c);

#endif
