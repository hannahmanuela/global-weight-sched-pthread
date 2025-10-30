#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "vt.h"
#include "group.h"
#include "heap.h"
#include "lheap.h"
#include "mheap.h"
#include "global_heap.h"
#include "util.h"

#define GRP2 2
#define NPROC 2

int num_cores;

void ticks_gettime(t_t *ticks) {
}

void ticks_getidle(t_t *ticks) {
}

void ticks_getwork(t_t *ticks) {
}

void test_one_heap() {
	printf("test_one_heap start\n");

	struct mheap *mh = mh_new(grp_cmp, 1, 1);
	struct group *gs[2];
	struct process *p;
	
	for (int i = 0; i < GRP2; i++) {
		gs[i] = grp_new(i, 10 * (i+1));
		mh_add_group(mh, gs[i]);
		for (int j = 0; j < NPROC; j++) {
			struct process *p = grp_new_process(j, gs[i]);
			enqueue(p);
		}

	}
	p = schedule(0, mh);
	assert(p != NULL);
	assert(p->group->group_id == GRP2-1);
	assert(p->group->vruntime == 50);
	p = schedule(0, mh);
	assert(p != NULL);
	assert(p->group->group_id == 0);
	assert(p->group->vruntime == 100);

	printf("test_one_heap ok\n");
}

void main(int argc, char *argv[]) {
	test_one_heap();
}

