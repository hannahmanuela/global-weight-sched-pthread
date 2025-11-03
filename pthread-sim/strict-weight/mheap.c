#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
#include <limits.h>

#include "vt.h"
#include "driver.h"
#include "group.h"
#include "lheap.h"
#include "mheap.h"

struct mheap *mh_new(int grp_cmp(void *, void *), int n, int seed, int tick_length) {
	srandom(seed);
	struct mheap *mh = malloc(sizeof(struct mheap));
	mh->lh = (struct lock_heap **) malloc(sizeof(struct lock_heap) * n);
	for (int i=0; i < n; i++) {
		mh->lh[i] = lh_new(grp_cmp);
		// insert a dummy element so that the heap always has one elemement
		struct group *dummy_grp = grp_new(mh, DUMMY, 0);
		struct group_shard* dummy = malloc(sizeof(struct group_shard));
		dummy->group = dummy_grp;
		dummy->nqueued = 1;
		dummy->vruntime = INT_MAX;
		heap_elem_init(&dummy->heap_elem, dummy);
		heap_push(mh->lh[i]->heap, &dummy->heap_elem);
	}
	mh->nheap = n;
	mh->tick_length = tick_length;
	return mh;
}

int mh_min_vrt(struct lock_heap *lh) {
	struct group_shard *min = (struct group_shard *) heap_min(lh->heap);
	long mvt = 0;
	if (min && !grp_shard_dummy(min)) {
		mvt = min->vruntime;
	}	
	return mvt;
}

static void print_elem(struct heap_elem *e) {
	struct group_shard *s = (struct group_shard *) e->elem;
	grp_shard_print(s);
}

void mh_print(struct mheap *mh) {
	printf("= mh tl %d\n", mh->tick_length);
	for (int i = 0; i < mh->nheap; i++) {
		struct heap *h = mh->lh[i]->heap;
		printf("Heap %d size %d: \n", i, h->heap_size);
		heap_iter(mh->lh[i]->heap, print_elem);
		printf("\n");
	}
	printf("=\n");
}

void mh_lock_stats(struct mheap *mh) {
	printf("= mh: lock stats: \n");
	for (int i = 0; i < mh->nheap; i++) {
		printf("== heap %d:\n", i);
		lh_stats(mh->lh[i]);
	}
	printf("=\n");
}

static void grp_stats(struct heap_elem *e, long sum) {
	struct group *g = (struct group *) e->elem;
	if (g->group_id == DUMMY)
		return;
	t_t t = ticks_sum(g->sleeptime);
	printf("%d: runtime %ld us sleeptime %ld us weight %d ticks %0.2f\n", g->group_id,
	       g->runtime, t,
	       g->weight, 1.0*g->runtime/(sum-t));
}

void mh_runtime_stats(struct mheap *mh) {
	t_t *ticks = new_ticks();
	ticks_gettime(ticks);
	t_t tot = ticks_sum(ticks);
	ticks_getwork(ticks);
	t_t work = ticks_sum(ticks);
	ticks_getidle(ticks);
	t_t idle = ticks_sum(ticks);
	printf("= mh: runtime stats total ticks %ld us work %ld us idle %ld us\n", tot, work, idle);
	for (int i = 0; i < mh->nheap; i++) {
		for (struct heap_elem *e = heap_first(mh->lh[i]->heap); e != NULL; e = heap_next(mh->lh[i]->heap, e)) {
			grp_stats(e, tot);
		}
	}
	printf("=\n");
}

struct lock_heap *mh_heap(struct mheap *mh, int i) {
	return mh->lh[i];
}

struct lock_heap *mh_choose_heap(struct mheap *mh) {
retry:
	int i = random() % mh->nheap;
	struct lock_heap *lh = mh_heap(mh, i);
	if(lh_try_lock(lh) != 0)
		goto retry;
	return lh;
}

// caller must hold heap and group lock
void mh_add_group_shard(struct group_shard *s, struct lock_heap *lh) {
	s->lh = lh;
	heap_push(lh->heap, &s->heap_elem);
}

// caller must hold heap and group lock
void mh_del_group_shard(struct mheap *mh, struct group_shard *g) {
	heap_remove_at(g->lh->heap, &g->heap_elem);
	g->lh = NULL;
}

// to sanity check; run with 1 core
void mh_check_min_group(struct mheap *mh, struct group_shard *s0) {
	struct group_shard *min;
	int n = 0;
	for (int i = 0; i < mh->nheap; i++) {
		struct lock_heap *lh = mh_heap(mh, i);
		struct group_shard *s1 = (struct group_shard *) heap_min(lh->heap);
		if(s1 && (s0->vruntime > s1->vruntime)) {
			min = s1;
			n++;
		}
	}
	if (min != NULL)
		printf("%ld(g%d,s%d) min %ld(g%d,s%d) n %d\n", s0->vruntime, s0->group->group_id, s0->shard_id, min->vruntime, min->group->group_id, min->shard_id, n);
}


// caller must ensure there is a min element
void *mh_min_atomic(struct lock_heap *lh)  {
        struct heap_elem *e = atomic_load(&(lh->heap->heap[0]));
        return e->elem;
}

// https://dl.acm.org/doi/10.1145/2755573.2755616
struct group_shard *mh_sample_min_group_shard(struct mheap *mh) {
retry:
	int i = random() % mh->nheap;
	int j = random() % mh->nheap;
	while (i == j) {
		j = random() % mh->nheap;
	}
	struct lock_heap *lh_i = mh_heap(mh, i);
	struct lock_heap *lh_j = mh_heap(mh, j);
	struct group_shard *s_i = (struct group_shard *) mh_min_atomic(lh_i);
	struct group_shard *s_j = (struct group_shard *) mh_min_atomic(lh_j);
	if (grp_shard_dummy(s_i) && grp_shard_dummy(s_j)) {
		return NULL;
	}
	if (grp_shard_dummy(s_i)) {
		s_i = s_j;
		lh_i = lh_j;
	} else if (s_j) {
		int vt_i = atomic_load(&s_i->vruntime);
		int vt_j = atomic_load(&s_j->vruntime);
		if (vt_i > vt_j) {
			s_i = s_j;
			lh_i = lh_j;
		}
		if (vt_i == vt_j) {
			int w_i = atomic_load(&s_i->weight);
			int w_j = atomic_load(&s_j->weight);
			if (w_j > w_i) {	
				s_i = s_j;
				lh_i = lh_j;
			}
		}
	}
	if(lh_try_lock(lh_i) != 0)
		goto retry;
	if ((struct group_shard *) heap_min(lh_i->heap) != s_i) {
		lh_unlock(lh_i);
		goto retry;
	}
	pthread_rwlock_wrlock(&s_i->shard_lock);
	return s_i;
}

// returns with heap and shard locked
struct group_shard *mh_min_group_shard(struct mheap *mh) {
	if (mh->nheap == 1) {
		struct lock_heap *lh = mh_heap(mh, 0);
		lh_lock_timed(lh);
		struct group_shard *s = (struct group_shard *) heap_min(lh->heap);
		if(!s || grp_shard_dummy(s)) {
			lh_unlock(lh);
			return NULL;
		}	
		if (s && s->nqueued == 0) {
			lh_unlock(lh);
			s = NULL;
		}
		if (s) {
			pthread_rwlock_wrlock(&s->shard_lock);
		}
		return s;
	}
	return mh_sample_min_group_shard(mh);
}
