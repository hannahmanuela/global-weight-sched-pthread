#include <stdlib.h>
#include <stdio.h>

#include "lheap.h"
#include "mheap.h"

struct mheap *mh_new(int grp_cmp(void *, void *), int n) {
	struct mheap *mh = malloc(sizeof(struct mheap));
	mh->lh = (struct lock_heap **) malloc(sizeof(struct lock_heap) * n);
	for (int i=0; i < n; i++) {
		mh->lh[i] = lh_new(grp_cmp);
	}
	mh->nheap = n;
	return mh;
}

void mh_print(struct mheap *mh, void print_elem(struct heap_elem*)) {
	for (int i = 0; i < mh->nheap; i++) {
		lh_rdlock_timed(mh->lh[i]);
		printf("Global heap size: %d \n", mh->lh[i]->heap->heap_size);
		printf("Heap contents by (group_id: svt, num_threads, num_queued): ");
		heap_iter(mh->lh[i]->heap, print_elem);
		printf("\n");
		lh_unlock(mh->lh[i]);
	}
}

void mh_stats(struct mheap *mh) {
	for (int i = 0; i < mh->nheap; i++) {
		lh_stats(mh->lh[i]);
	}
}

struct lock_heap *mh_heap(struct mheap *mh, int i) {
	return mh->lh[i];
}
