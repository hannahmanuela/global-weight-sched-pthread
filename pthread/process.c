#include <stdio.h>
#include <stdlib.h>

#include "group.h"
#include "process.h"

void proc_print(struct task_struct *p) {
	printf("[pid %d(%d) vt %lld w %d]", p->pid, p->group->gid,  p->he.vruntime, p->he.weight);
}	

struct task_struct *proc_new(int id, int w) {
	struct task_struct *p = aligned_alloc(CACHE_LINE_SZ, ALIGN_UP(sizeof(struct task_struct), CACHE_LINE_SZ));
	p->pid = id;
	p->runtime = 0;
	p->group = NULL;
	p->next = NULL;
	heap_elem_init(&p->he, 0, w);
	heap_elem_init(&p->he_r, 0, 0);
	// lock_init(&p->lk);
	p->h = NULL;
	return p;
}
