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

#define N 100000

int num_cores;

void ticks_gettime(t_t *ticks) {
}

void ticks_getidle(t_t *ticks) {
}

void ticks_getwork(t_t *ticks) {
}

void main(int argc, char *argv[]) {
	int sum = 0;
	int seed = getpid();
	int worst;
	for(int t = 0; t < N; t++) {
		struct mheap *mh = mh_new(grp_cmp, atoi(argv[1]), seed+t);
		struct group *g = grp_new(0, 10);
		mh_add_group(mh, g);

		struct process *p = grp_new_process(1, g);
		enqueue(p);

		for (int i = 0; ; i++) {
			if ((p = schedule(0, mh)) != NULL) {
				sum += i;
				if(i > worst)
					worst = i;
				break;
			}
		}
	}
	printf("avg %d worst %d\n", sum/N, worst);
}

