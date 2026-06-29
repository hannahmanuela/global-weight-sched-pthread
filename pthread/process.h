#ifndef _PROCESS_H_

#define _PROCESS_H_

#include "util.h"
#include "vt.h"
#include "heap_elem.h"
#include "heap.h"

struct group;
struct mheap;

#define RR_HIGH 0
#define RR_LOW 1

#define LOW_VT ((vt_t)(1L << 32))

struct task_struct {
	// for runnable mheap
	struct heap_elem he;
	struct heap *h;   // for gwfs and pcrq

	// for running mheap
	struct heap_elem he_r;
	struct heap *h_r;

	t_t runtime;  // __calign__;  // number of us the process ran

	struct group *group;

	int pid;
	struct task_struct *next;
	
        struct spinlock lk __calign__;

	int cid __calign__;     // core that is running or ran last this process
	
} __calign__;

void proc_print(struct task_struct *p);
void proc_mh_print(struct mheap *mh);
void proc_mh_r_print(struct mheap *mh);
struct task_struct *proc_new(int id, int weight);
int proc_cmp(struct heap_elem *e0, struct heap_elem *e1);
void proc_set_vt_prio(struct task_struct *p);

#endif

