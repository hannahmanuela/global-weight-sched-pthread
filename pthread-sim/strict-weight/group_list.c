#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "heap.h"
#include "group.h"
#include "group_list.h"
#include "util.h"

// lock invariant (to avoid deadlocks): 
//  - always lock global list before group lock, if you're going to lock both 
//      (no locking a group while holding the list lock)

struct lock_heap *lh_new() {
	struct lock_heap *lh = (struct lock_heap *) malloc(sizeof(struct lock_heap));
	lh->heap = heap_new(grp_cmp);
	lh->wait_for_wr_heap_lock_cycles = 0;
	lh->num_times_wr_heap_locked = 0;
	atomic_init(&lh->wait_for_rd_heap_lock_cycles, 0);
	atomic_init(&lh->num_times_rd_heap_locked, 0);
	pthread_rwlock_init(&lh->heap_lock, NULL);
	return lh;
}

struct group_list *gl_new() {
	struct group_list *glist = (struct group_list *) malloc(sizeof(struct group_list));
	glist = (struct group_list *) malloc(sizeof(struct group_list));
	glist->lheap = lh_new();
	return glist;
}

void gl_unlock(struct lock_heap *lh) {
	pthread_rwlock_unlock(&lh->heap_lock);
}

// Wrapper functions for pthread_rwlock operations with timing
static void timed_pthread_rwlock_wrlock_group_list(struct group_list *glist, pthread_rwlock_t *lock) {
	int start_tsc = safe_read_tsc();
	pthread_rwlock_wrlock(lock);
	int end_tsc = safe_read_tsc();
	glist->lheap->wait_for_wr_heap_lock_cycles += (end_tsc - start_tsc);
	glist->lheap->num_times_wr_heap_locked++;
}

static void timed_pthread_rwlock_rdlock_group_list(struct group_list *glist, pthread_rwlock_t *lock) {
	int start_tsc = safe_read_tsc();
	pthread_rwlock_rdlock(lock);
	int end_tsc = safe_read_tsc();
	atomic_fetch_add_explicit(&glist->lheap->wait_for_rd_heap_lock_cycles, (end_tsc - start_tsc), memory_order_relaxed);
	atomic_fetch_add_explicit(&glist->lheap->num_times_rd_heap_locked, 1, memory_order_relaxed);
}

static void print_elem(struct heap_elem *e) {
	struct group *g = (struct group *) e->elem;
	pthread_rwlock_rdlock(&g->group_lock);
	printf("(%d: %d, %d, %d)", g->group_id, g->spec_virt_time, g->num_threads, g->threads_queued);
	pthread_rwlock_unlock(&g->group_lock);
}

void gl_print(struct group_list *gl) {
	timed_pthread_rwlock_rdlock_group_list(gl, &gl->lheap->heap_lock);
	printf("Global heap size: %d \n", gl->lheap->heap->heap_size  );
	printf("Heap contents by (group_id: svt, num_threads, num_queued): ");
	heap_iter(gl->lheap->heap, print_elem);
	printf("\n");
	pthread_rwlock_unlock(&gl->lheap->heap_lock);
}

void gl_stats(struct group_list *glist) {
    printf("\nLock timing statistics:\n");
    if (glist->lheap->num_times_wr_heap_locked > 0) {
        printf("Group list write lock: avg %ld cycles (%ld total cycles, %ld operations)\n", 
               glist->lheap->wait_for_wr_heap_lock_cycles / glist->lheap->num_times_wr_heap_locked,
               glist->lheap->wait_for_wr_heap_lock_cycles, glist->lheap->num_times_wr_heap_locked);
    }
    if (glist->lheap->num_times_rd_heap_locked > 0) {
        printf("Group list read lock: avg %ld cycles (%ld total cycles, %ld operations)\n", 
               glist->lheap->wait_for_rd_heap_lock_cycles / glist->lheap->num_times_rd_heap_locked,
               glist->lheap->wait_for_rd_heap_lock_cycles, glist->lheap->num_times_rd_heap_locked);
    }
}


// peek min group without removing; 
// returns with group and list locked
struct group* gl_peek_min_group(struct group_list *gl, struct lock_heap **lh) {
	timed_pthread_rwlock_wrlock_group_list(gl, &gl->lheap->heap_lock);
	struct group *g = (struct group *) heap_min(gl->lheap->heap);
	*lh = gl->lheap;
	if (g && g->threads_queued == 0) {
		g = NULL;
	}
	if (g) {
		pthread_rwlock_wrlock(&g->group_lock);
	}
	return g;
}


// compute avg_spec_virt_time for groups in gl, ignoring group_to_ignore
// caller should have no locks
int gl_avg_spec_virt_time(struct group_list *gl, struct group *group_to_ignore) {
	int total_spec_virt_time = 0;
	int count = 0;
	timed_pthread_rwlock_rdlock_group_list(gl, &gl->lheap->heap_lock);
	for (struct heap_elem *e = heap_first(gl->lheap->heap); e != NULL; e = heap_next(gl->lheap->heap, e)) {
		struct group *g = (struct group *) e->elem;
		assert(g != NULL);
		if (g == group_to_ignore) continue;
		total_spec_virt_time += grp_get_spec_virt_time(g);
		count++;
	}
	pthread_rwlock_unlock(&gl->lheap->heap_lock);
	if (count == 0) return 0;
	return total_spec_virt_time / count;
}


// add group to heap; caller must hold heap_lock
void gl_add_group(struct group_list *gl, struct group *g) {
	heap_push(gl->lheap->heap, &g->heap_elem);
}

// delete group from heap; caller must hold heap_lock
void gl_del_group(struct group_list *gl, struct group *g) {
	assert(g->heap_elem.heap_index == -1);
	heap_remove_at(gl->lheap->heap, &g->heap_elem);
}

// Update group's spec_virt_time and safely reheapify if the group is in the heap.
// caller must hold group_list_lock and group_lock
// keeps both lock held
void gl_update_group_svt(struct group_list *gl, struct group *g, int diff) {
	g->spec_virt_time += diff;
	heap_fix_index(gl->lheap->heap, &g->heap_elem);
}

void gl_fix_group(struct group_list *gl, struct group *g) {
	timed_pthread_rwlock_wrlock_group_list(gl, &gl->lheap->heap_lock);
        pthread_rwlock_wrlock(&g->group_lock);
        // If the group is currently in the heap, fix its position
        if (g->heap_elem.heap_index != -1) {
		heap_fix_index(gl->lheap->heap, &g->heap_elem);
        }
        pthread_rwlock_unlock(&g->group_lock);
        pthread_rwlock_unlock(&gl->lheap->heap_lock);
}

void gl_register_group(struct group_list *gl, struct group *g) {
	timed_pthread_rwlock_wrlock_group_list(gl, &gl->lheap->heap_lock);
	gl_add_group(gl, g);
	pthread_rwlock_unlock(&gl->lheap->heap_lock);
}

void gl_unregister_group(struct group_list *gl, struct group *g) {
	timed_pthread_rwlock_wrlock_group_list(gl, &gl->lheap->heap_lock);
	gl_del_group(gl, g);
	pthread_rwlock_unlock(&gl->lheap->heap_lock);
}
