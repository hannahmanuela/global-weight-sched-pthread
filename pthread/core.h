#ifndef _CORE_H_

#define _CORE_H_

#include <stdlib.h>
#include <stdatomic.h>

#include "vt.h"
#include "util.h"
#include "lock.h"
#include "dllist.h"

struct log_entry {
	long ts_in;
	long ts_out;
	vt_t vt;
	int cid;
	int pid;
	int gid;
	w_t w;
	short hid;
};

struct core {
	pthread_t tid;
	
	struct spinlock lk __calign__;

	_Atomic(int)  preempted __calign__;

	dlnode_t preempt_node __calign__;

	int cid __calign__;

	unsigned int seed;
	struct drand48_data randBuffer;

	struct task_struct *process;   // currently running process or last process ran
	bool scan_high;                // set to true when core should scan for high procs
	struct task_struct *pool;      // pool of processes sleeping


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
	long nhint;
	long nrand;

	long offset_sub_retry;

	long npreempt_set;
	long npreempt_clear;
	long npreempt_find_ok;
	long npreempt_find_fail;
	long npreempt_retry;

	long nskip_high;
	long nscan_all;
	long nscan_all_ok;
	long npreempted;
	long nlocal;
	long ndelay_yield;

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

void set_mycore(struct core *);
struct core *mycore();
int calc_pin_cpu(int cid);
void core_print(struct core *c);
void c_print(struct core *c, int ngrp);
int c_rand(int n);
struct core *c_new(int i, int n, int seed);
void c_log_init(struct core *c, char *name);
void c_log_append(struct task_struct *p);
void c_log_done(struct core *c);
void c_lat(struct task_struct *p);

#endif
