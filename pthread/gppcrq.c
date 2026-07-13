#include <stdio.h>
#include <assert.h>

#include "core.h"
#include "sched_state.h"
#include "mheap.h"
#include "pcrq.h"

// per-core runqueue scheduler with global priority

extern bool debug;
extern int num_groups;
extern struct sched_state *ss_global;

#define IND(mh, i) ((i) % mh->nheap)

// XXX use mheap_all?
static struct heap_elem *global_high(struct mheap *mh) {
	struct heap_elem *he = mh_deq_min_elem_all_heap(mh,  is_min_elem_high, -1);
	return he;
}

struct task_struct *ss_schedule_gppcrq(struct task_struct *prev) {
	struct core *c = mycore();
	struct heap *h = ss_global->mh->h[c->cid];

	lock_acquire(&h->lk);

	if (prev != NULL) {
		assert(prev->h == h);
		heap_push(prev->h, &prev->he);
		prev->he.tsc_in = safe_read_tsc();
		if(debug) {
			printf("%d(%d): yield_gppcrq %d\n", prev->pid, prev->group->gid, c->cid);
		}
	}

	struct heap_elem *he = heap_min(h);
	bool look_for_high = ((he == NULL) || (he->weight == W_LOW));
	if(!look_for_high) {
		mycore()->nlocal += 1;
		if ((he = heap_remove_min(h)) != NULL) {
			he->tsc_out = safe_read_tsc(); 
		}
	}
	lock_release(&h->lk);

	if(look_for_high) {
		he = global_high(ss_global->mh);
		if(he == NULL) {
			lock_acquire(&h->lk);
			if ((he = heap_remove_min(h)) != NULL) {
				he->tsc_out = safe_read_tsc(); 
			}
			lock_release(&h->lk);
		}
	}

	if(he == NULL) {
		return NULL;
	}

        assert(he->vruntime != DUMMY);
	struct task_struct *p = container_of(he, struct task_struct, he);
	if(debug) {
		printf("%d: schedule_gppcrq %d(%d)\n", c->cid, p->pid, p->group->gid);
	}
	if(c->fd > 0) {
		c_log_append(p);
	}
	if(p->he.weight == W_LOW) {
		mycore()->nskip_high++;
	}
	p->h = h;
	return p;
}

void ss_yield_gppcrq(struct task_struct *p, t_t time_passed) {
	p->runtime += time_passed;
}

void ss_enqueue_gppcrq(struct task_struct *p) {
	struct core *c = mycore();
	int i = 0, j;
	if(ss_global->mh->nheap > 1) {
		mh_rand_heaps(ss_global->mh, &i, &j);
		if (ss_global->mh->h[i]->heap_size > ss_global->mh->h[j]->heap_size)
			i = j;
	}
	struct heap *h = ss_global->mh->h[i];
	lock_acquire(&h->lk);
	p->h = h;
	p->he.tsc_in = safe_read_tsc();
	heap_push(h, &p->he);
	if(debug) {
		printf("%d(%d): enqueue_gppcrq at %d\n", p->pid, p->group->gid, i);
	}
	lock_release(&h->lk);
}

void ss_dequeue_gppcrq(struct task_struct *p, t_t time_passed) {
	p->runtime += time_passed;
	if(debug) {
		printf("%d: %d(%d): dequeue_gppcrq %ld\n", mycore()->cid, p->pid, p->group->gid, time_passed);
		//mh_print(p->group->mh);
	}
}
