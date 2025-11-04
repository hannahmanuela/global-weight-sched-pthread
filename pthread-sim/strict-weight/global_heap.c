#include <assert.h>
#include <limits.h>
#include <stdio.h>

#include "vt.h"
#include "driver.h"
#include "lheap.h"
#include "group.h"
#include "mheap.h"

bool debug;

// Select next process to run
struct process *schedule(int core, struct mheap *mh) {
	struct group_shard *min_group_shard = mh_min_group_shard(mh);
	if (min_group_shard == NULL) {
		return NULL;
	}

    // mh_min_group_shard returns with heap and group shard lock held
    
	if(debug) {
		printf("g%d,s%d(%d): schedule\n", min_group_shard->group->group_id, min_group_shard->shard_id, core);
		mh_print(min_group_shard->mh);
	}

	grp_shard_upd_vruntime(min_group_shard, mh->tick_length);

	// select the next process
	struct process *next_p = grp_shard_deq_process(min_group_shard);
	assert(next_p != NULL);
	atomic_fetch_add(&next_p->group->nrunning, 1);
	min_group_shard->nrunning += 1;
	
	// must be after grp_deq_process, since it may empty the proc queue
	heap_fix_index(min_group_shard->lh->heap, &min_group_shard->heap_elem);

	pthread_rwlock_unlock(&min_group_shard->shard_lock);
	lh_unlock(min_group_shard->lh);

	return next_p;
}

// Make p runnable, which may make the group runnable.
void enqueue(struct process *p) {

	p->group_shard = grp_pick_shard(p->group);

	pthread_rwlock_wrlock(&p->group_shard->shard_lock);
	bool none_queued = p->group_shard->nqueued == 0;
	bool was_sleep = grp_shard_is_sleep(p->group_shard);

	grp_add_processL(p); // this sets p->group_shard; returns with shard lock held
	
	p->group_shard->nthread += 1;
	atomic_fetch_add(&p->group->nthread, 1);

	pthread_rwlock_unlock(&p->group_shard->shard_lock);

	if(debug) {
		printf("g%d,s%d(%d): enqueue was_sleep %d lh%p\n", p->group->group_id, p->group_shard->shard_id, p->core_id, was_sleep, p->group_shard->lh);
		mh_print(p->group_shard->mh);
	}
	
	// TODO: where do I lock the heap?
	if(none_queued && !was_sleep)
		heap_fix_index(p->group_shard->lh->heap, &p->group_shard->heap_elem);

	if (was_sleep) {
		grp_shard_enqueue(p->group_shard); // locks group lock
	} 
}

// Process p yields core
static bool yieldL(struct process *p, int time_passed) {
	p->group->runtime += time_passed;
	p->group->nrunning -= 1;
	p->group_shard->nrunning -= 1;
	return grp_shard_adjust_vruntime(p->group_shard, time_passed, p->group_shard->mh->tick_length);
}

// Yield and enqueue
void yield(struct process *p, t_t time_passed) {
	lh_lock_timed(p->group_shard->lh);
	if(debug) {
		printf("g%d,s%d(%d): yield time_passed %ld\n", p->group->group_id, p->group_shard->shard_id, p->core_id, time_passed);
		mh_print(p->group_shard->mh);
	}
	pthread_rwlock_wrlock(&p->group_shard->shard_lock);
	grp_add_processL(p);   // now p->group_shard is set, new shard has procs queued; fix heap
	bool none_queued = p->group_shard->nqueued == 1;
	bool fix_heap = yieldL(p, time_passed);
	if(none_queued || fix_heap)
		heap_fix_index(p->group_shard->lh->heap, &p->group_shard->heap_elem);
	pthread_rwlock_unlock(&p->group_shard->shard_lock);
	lh_unlock(p->group_shard->lh);
}

// Process p is not runnable and yields core, which may make
// p's group not runnable
void dequeue(struct process *p, t_t time_passed) {
	struct lock_heap *lh = p->group_shard->lh;
	lh_lock_timed(lh);
	pthread_rwlock_wrlock(&p->group_shard->shard_lock);

	if(debug) {
		printf("g%d,s%d(%d): dequeue %ld\n", p->group->group_id, p->group_shard->shard_id, p->core_id, time_passed);
		mh_print(p->group_shard->mh);
	}

	atomic_fetch_sub(&p->group->nthread, 1);
	p->group_shard->nthread -= 1;

	assert(p->group_shard->nthread >= p->group_shard->nqueued);
	bool fix_heap = yieldL(p, time_passed);
	bool is_sleep = grp_shard_is_sleep(p->group_shard);
	if (fix_heap) {
		heap_fix_index(lh->heap, &p->group_shard->heap_elem);
	}
	if (is_sleep) {
		grp_shard_lag_vruntime(p->group_shard, mh_min_vrt(lh));
		mh_del_group_shard(p->group_shard->mh, p->group_shard);
		ticks_gettime(p->group->sleepstart);
	}
	pthread_rwlock_unlock(&p->group_shard->shard_lock);
	lh_unlock(lh);
}
