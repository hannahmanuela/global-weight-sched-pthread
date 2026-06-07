#ifndef _PROCESS_H_

#define _PROCESS_H_

#include "util.h"
#include "vt.h"
#include "heap_elem.h"
#include "heap.h"

struct group;

struct task_struct {
	struct heap_elem he;
	struct heap *h;

	t_t runtime;  // __calign__;  // number of us the process ran

	struct mheap *mh;
	struct group *group;

	int pid;
	struct task_struct *next;
	
	long tsc;
	int other_hid;
	vt_t other_vt;

	int cid __calign__;     // core that is running or ran last this process
	
} __calign__;

void proc_print(struct task_struct *p);
struct task_struct *proc_new(struct mheap *mh, int id, int weight);
int proc_cmp(struct heap_elem *e0, struct heap_elem *e1);

#endif

