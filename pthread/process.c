#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <assert.h>

#include "group.h"
#include "process.h"

void proc_print(struct task_struct *p) {
	printf("[pid %d(%d) vt %lld w %d]", p->pid, p->group->gid,  atomic_load(&p->he.vruntime), atomic_load(&p->he.weight));
}	

struct task_struct *proc_new(int id, int w) {
	struct task_struct *p = aligned_alloc(CACHE_LINE_SZ, ALIGN_UP(sizeof(struct task_struct), CACHE_LINE_SZ));
	p->pid = id;
	p->cid = -1;
	p->runtime = 0;
	p->group = NULL;
	p->next = NULL;
	heap_elem_init(&p->he, 0, w);
	heap_elem_init(&p->he_r, 0, 0);
	lock_init(&p->lk);
	p->h = NULL;
	p->h_r = -1;;
	return p;
}

static void proc_heap_elem_print(struct heap_elem *he) {
	if(he->vruntime == DUMMY) {
		printf("[dummy vt %lld w %d]", he->vruntime, he->weight);
		return;
	}	
	struct task_struct *p = container_of(he, struct task_struct, he);
	printf("("); proc_print(p); printf(")");
}

static void proc_heap_elem_r_print(struct heap_elem *he) {
	if(he->vruntime == DUMMY) {
		printf("[dummy vt %lld w %d]", he->vruntime, he->weight);
		return;
	}
	struct task_struct *p = container_of(he, struct task_struct, he_r);
	printf("("); proc_print(p); printf(")");
}


void proc_mh_print(struct mheap *mh) {
	mh_print(mh, proc_heap_elem_print);
}

void proc_mh_r_print(struct mheap *mh) {
	mh_print(mh, proc_heap_elem_r_print);
}

