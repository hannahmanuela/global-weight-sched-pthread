#ifndef _CORE_H_

#define _CORE_H_

#include <stdatomic.h>

#include "vt.h"
#include "util.h"
#include "lock.h"

struct log_entry {
	long ts;
	vt_t vt;
	int cid;
	int pid;
	int gid;
	w_t w;
	int hid;
	int ohid;
	vt_t ovt;
};

struct core {
	struct spinlock lk __calign__;
	
	int cid;
	unsigned int seed;

	struct process *process;   // currently running process or last process ran
	struct process *rqueue;    // local run queue
	struct process *pool;   // pool of processes sleeping

	// fields for tatistics:
	t_t work;
	t_t idle;
	t_t total;

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

	long lag_sub_retry;

	long npreempt_set;
	long npreempt_retry;

	long nlocal;

	long nmc_is_zero;
	long nmc_dec;
	long nmc_inc;

	int *hit;
	int *miss;

	struct log_entry *log;
	int log_nentry;
	int fd;

} __calign__;

#define LOG_NENTRY  1000000

int calc_pin_cpu(int cid);
void core_print(struct core *c);
void c_print(struct core *c, int ngrp);
int c_rand(struct core *c, int n);
struct core *c_new(int i, int n, int seed);
void c_log_init(struct core *c, char *name);
void c_log_append(struct core *c, struct process *p);
void c_log_done(struct core *c);

#endif
