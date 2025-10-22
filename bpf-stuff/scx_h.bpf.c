#include <scx/common.bpf.h>

char _license[] SEC("license") = "GPL";

UEI_DEFINE(uei);


/* NOTES
* - for now, tasks are fifo
* 
*/

// =======================================================
// DEFINES
// =======================================================

#define MY_SLICE ((__u64)4 * 1000000) // 4ms
#define TMP_GLOBAL_Q 0
#define CGROUP_MAX_RETRIES 100

const volatile u64 cgrp_slice_ns = MY_SLICE;

enum {
	FCG_HWEIGHT_ONE		= 1LLU << 16,	/* Base weight unit (65536) for hierarchical weight calculations. Used as the denominator when computing proportional shares across the flattened cgroup hierarchy. */
};

// =======================================================
// GLOBAL VARS
// =======================================================

static __u64 global_num_contending_grps;
static __u64 global_curr_sum_svt;


// =======================================================
// DATA STRUCTURES
// =======================================================


struct core_info {
	u64			cur_cgid; // cgroup id core is currently running
	u64			cur_at; // actual timestamp of ??? when core last scheduled?
};

struct cgrp_node {
	struct bpf_rb_node	rb_node; // tree node of cgroup
	__u64			svt; // spec virt time
	__u64			cgid; // cgroup id
};

struct cgrp_info {
	u32			nr_active; // nr of threads in the cycle (bookmarked by runnable and quescient) [TODO: given the below, not sure how it's ok to sometimes not update nr_active?]
	u32			nr_runnable; //  In leaf cgroups, ->nr_runnable which is updated with __sync operations gates ->nr_active updates, so that we don't have to grab the cgv_tree_lock repeatedly for a busy cgroup which is staying active
	u32			queued; // used to synchronize between pick_min_grp and enq -> former sets it to 0, latter to 1 (used nowhere else?)
	u32			weight; // base weight of the group (unflattened!)
	u32			hweight; // effective/globalized weight of the group [Calculated by compounding at each level: hweight = parent_hweight * weight / parent_child_weight_sum]
	u64			child_weight_sum; // Sum of weights of all active child cgroups. Used as denominator when calculating hierarchical weights for children. Updated when child cgroups become active/inactive.
	u64			hweight_gen; // Generation counter for hierarchical weight caching. Incremented when weight tree changes to invalidate cached hweight values. Prevents stale weight calculations.
	s64			svt_delta; // a cache for the svt updates to the group; synced to the groups tree node before the node is added back to the tree
	u64			tvtime_now; // tasks (max) vtime; used in cgroup_move, init_task
};

// =======================================================
// MAPS
// =======================================================


struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__type(key, u32);
	__type(value, struct core_info);
	__uint(max_entries, 1);
} core_info_storage SEC(".maps"); // cpu info, a per-cpu array

struct {
	__uint(type, BPF_MAP_TYPE_CGRP_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, struct cgrp_info);
} cgrp_info_storage SEC(".maps"); // main per cgroup storage; NOT the tree

private(CGV_TREE) struct bpf_spin_lock cg_tree_lock;
private(CGV_TREE) struct bpf_rb_root cg_tree __contains(cgrp_node, rb_node); // this is the tree

struct cgrp_node_stash {
	struct cgrp_node __kptr *node;
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 16384);
	__type(key, __u64);
	__type(value, struct cgrp_node_stash);
} cgrp_node_stash SEC(".maps"); // stash of cgroup tree nodes, for groups that are currently not runnable

/* gets inc'd on weight tree changes to expire the cached hweights */
u64 hweight_gen = 1;


// =======================================================
// TREE HELPER FUNCTIONS
// =======================================================

static u64 div_round_up(u64 dividend, u64 divisor)
{
	return (dividend + divisor - 1) / divisor;
}

static s64 signed_div(s64 a, s64 b) {
    bool aneg = a < 0;
    bool bneg = b < 0;
    // get the absolute positive value of both
    u64 adiv = aneg ? -a : a;
    u64 bdiv = bneg ? -b : b;
    // Do udiv
    u64 out = adiv / bdiv;
    // Make output negative if one or the other is negative, not both
    return aneg != bneg ? -out : out;
}

static struct core_state *find_cpu_ctx(void)
{
	struct core_state *cpuc;
	u32 idx = 0;

	cpuc = bpf_map_lookup_elem(&core_info_storage, &idx);
	if (!cpuc) {
		scx_bpf_error("cpu_ctx lookup failed");
		return NULL;
	}
	return cpuc;
}

static struct cgrp_info *find_cgrp_info(struct cgroup *cgrp)
{
	struct cgrp_info *gi;

	gi = bpf_cgrp_storage_get(&cgrp_info_storage, cgrp, 0, 0);
	if (!gi) {
		scx_bpf_error("cgrp_ctx lookup failed for cgid %llu", cgrp->kn->id);
		return NULL;
	}
	return gi;
}

static struct cgrp_info *find_ancestor_cgrp_info(struct cgroup *cgrp, int level)
{
	struct cgrp_info *cgi;

	cgrp = bpf_cgroup_ancestor(cgrp, level);
	if (!cgrp) {
		scx_bpf_error("ancestor cgroup lookup failed");
		return NULL;
	}

	cgi = find_cgrp_info(cgrp);
	if (!cgi)
		scx_bpf_error("ancestor cgrp_ctx lookup failed");
	bpf_cgroup_release(cgrp);
	return cgi;
}

static void cgrp_refresh_hweight(struct cgroup *cgrp, struct cgrp_info *cgi)
{
	int level;

	if (!cgi->nr_active) {
		return;
	}

	if (cgi->hweight_gen == hweight_gen) {
		return;
	}

	bpf_for(level, 0, cgrp->level + 1) {
		struct cgrp_info *cgi;
		bool is_active;

		cgi = find_ancestor_cgrp_info(cgrp, level);
		if (!cgi)
			break;

		if (!level) {
			cgi->hweight = FCG_HWEIGHT_ONE;
			cgi->hweight_gen = hweight_gen;
		} else {
			struct cgrp_info *pred_cgi;

			pred_cgi = find_ancestor_cgrp_info(cgrp, level - 1);
			if (!pred_cgi)
				break;

			/*
			 * We can be opportunistic here and not grab the
			 * cgv_tree_lock and deal with the occasional races.
			 * However, hweight updates are already cached and
			 * relatively low-frequency. Let's just do the
			 * straightforward thing.
			 */
			bpf_spin_lock(&cg_tree_lock);
			is_active = cgi->nr_active;
			if (is_active) {
				cgi->hweight_gen = pred_cgi->hweight_gen;
				cgi->hweight =
					div_round_up(pred_cgi->hweight * cgi->weight,
						     pred_cgi->child_weight_sum);
			}
			bpf_spin_unlock(&cg_tree_lock);

			if (!is_active) {
				break;
			}
		}
	}
}

/*
 * Walk the cgroup tree to update the active weight sums as tasks wake up and
 * sleep. The weight sums are used as the base when calculating the proportion a
 * given cgroup or task is entitled to at each level.
 */
static void update_active_weight_sums(struct cgroup *cgrp, bool runnable)
{
	struct cgrp_info *cgi;
	bool updated = false;
	int idx;

	cgi = find_cgrp_info(cgrp);
	if (!cgi)
		return;

	/*
	 * In most cases, a hot cgroup would have multiple threads going to
	 * sleep and waking up while the whole cgroup stays active. In leaf
	 * cgroups, ->nr_runnable which is updated with __sync operations gates
	 * ->nr_active updates, so that we don't have to grab the cgv_tree_lock
	 * repeatedly for a busy cgroup which is staying active.
	 */
	if (runnable) {
		if (__sync_fetch_and_add(&cgi->nr_runnable, 1))
			return;
	} else {
		if (__sync_sub_and_fetch(&cgi->nr_runnable, 1))
			return;
	}

	/*
	 * If @cgrp is becoming runnable, its hweight should be refreshed after
	 * it's added to the weight tree so that enqueue has the up-to-date
	 * value. If @cgrp is becoming quiescent, the hweight should be
	 * refreshed before it's removed from the weight tree so that the usage
	 * charging which happens afterwards has access to the latest value.
	 */
	if (!runnable)
		cgrp_refresh_hweight(cgrp, cgi);

	/* propagate upwards */
	bpf_for(idx, 0, cgrp->level) {
		int level = cgrp->level - idx;
		struct cgrp_info *cgi, *pred_cgi = NULL;
		bool propagate = false;

		cgi = find_ancestor_cgrp_info(cgrp, level);
		if (!cgi)
			break;
		if (level) {
			pred_cgi = find_ancestor_cgrp_info(cgrp, level - 1);
			if (!pred_cgi)
				break;
		}

		/*
		 * We need the propagation protected by a lock to synchronize
		 * against weight changes. There's no reason to drop the lock at
		 * each level but bpf_spin_lock() doesn't want any function
		 * calls while locked.
		 */
		bpf_spin_lock(&cg_tree_lock);

		if (runnable) {
			if (!cgi->nr_active++) {
				updated = true;
				if (pred_cgi) {
					propagate = true;
					pred_cgi->child_weight_sum += cgi->weight;
				}
			}
		} else {
			if (!--cgi->nr_active) {
				updated = true;
				if (pred_cgi) {
					propagate = true;
					pred_cgi->child_weight_sum -= cgi->weight;
				}
			}
		}

		bpf_spin_unlock(&cg_tree_lock);

		if (!propagate)
			break;
	}

	if (updated)
		__sync_fetch_and_add(&hweight_gen, 1);

	if (runnable)
		cgrp_refresh_hweight(cgrp, cgi);
}



// =======================================================
// ACCOUNTING HELPER FUNCTIONS
// =======================================================


static inline u64 gl_avg_svt() {
    u64 sum_svt = __atomic_load_n(&global_curr_sum_svt, __ATOMIC_ACQUIRE);
    u64 ct = __atomic_load_n(&global_num_contending_grps, __ATOMIC_ACQUIRE);
    if (ct == 0) {
        return 0;
    }
    return sum_svt / ct;
}

static bool cg_node_less(struct bpf_rb_node *a, const struct bpf_rb_node *b)
{
	struct cgrp_node *cgn_a, *cgn_b;

	cgn_a = container_of(a, struct cgrp_node, rb_node);
	cgn_b = container_of(b, struct cgrp_node, rb_node);

	return cgn_a->svt < cgn_b->svt;
}










// =======================================================
// CGROUP BPF OPS
// =======================================================

void BPF_STRUCT_OPS(h_cgroup_set_weight, struct cgroup *cgrp, u32 weight)
{
	struct cgrp_info *cgi, *pred_cgi = NULL;

	cgi = find_cgrp_info(cgrp);
	if (!cgi)
		return;

	if (cgrp->level) {
		pred_cgi = find_ancestor_cgrp_info(cgrp, cgrp->level - 1);
		if (!pred_cgi)
			return;
	}

	bpf_spin_lock(&cg_tree_lock);
	if (pred_cgi && cgi->nr_active)
		pred_cgi->child_weight_sum += (s64)weight - cgi->weight;
	cgi->weight = weight;
	bpf_spin_unlock(&cg_tree_lock);
}

int BPF_STRUCT_OPS_SLEEPABLE(h_cgroup_init, struct cgroup *cgrp,
			     struct scx_cgroup_init_args *args)
{
	struct cgrp_info *cgi;
	struct cgrp_node *cg_node;
	struct cgrp_node_stash empty_stash = {}, *stash;
	u64 cgid = cgrp->kn->id;
	int ret;

	/*
	 * Technically incorrect as cgroup ID is full 64bit while dsq ID is
	 * 63bit. Should not be a problem in practice and easy to spot in the
	 * unlikely case that it breaks.
	 */
	ret = scx_bpf_create_dsq(cgid, -1);
	if (ret)
		return ret;

	cgi = bpf_cgrp_storage_get(&cgrp_info_storage, cgrp, 0,
				   BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (!cgi) {
		ret = -ENOMEM;
		goto err_destroy_dsq;
	}

	cgi->weight = args->weight;
	cgi->hweight = FCG_HWEIGHT_ONE;

	ret = bpf_map_update_elem(&cgrp_node_stash, &cgid, &empty_stash,
				  BPF_NOEXIST);
	if (ret) {
		if (ret != -ENOMEM)
			scx_bpf_error("unexpected stash creation error (%d)",
				      ret);
		goto err_destroy_dsq;
	}

	bpf_printk("CGROUP_INIT grp w/ weight %d", cgi->weight);

	stash = bpf_map_lookup_elem(&cgrp_node_stash, &cgid);
	if (!stash) {
		scx_bpf_error("unexpected cgv_node stash lookup failure");
		ret = -ENOENT;
		goto err_destroy_dsq;
	}

	cg_node = bpf_obj_new(struct cgrp_node);
	if (!cg_node) {
		ret = -ENOMEM;
		goto err_del_cg_node;
	}

	cg_node->cgid = cgid;
	cg_node->svt = 0; // TODO: this needs to be reasonable

	cg_node = bpf_kptr_xchg(&stash->node, cg_node);
	if (cg_node) {
		scx_bpf_error("unexpected !NULL cgv_node stash");
		ret = -EBUSY;
		goto err_drop;
	}

    // TODO: add to tree if we want the tree to also have the "empty" groups on it

	return 0;

err_drop:
	bpf_obj_drop(cg_node);
err_del_cg_node:
	bpf_map_delete_elem(&cgrp_node_stash, &cgid);
err_destroy_dsq:
	scx_bpf_destroy_dsq(cgid);
	return ret;
}

void BPF_STRUCT_OPS(h_cgroup_exit, struct cgroup *cgrp)
{
	u64 cgid = cgrp->kn->id;

	/*
	 * For now, there's no way find and remove the cgv_node if it's on the
	 * cgv_tree. Let's drain them in the dispatch path as they get popped
	 * off the front of the tree.
	 */
	bpf_map_delete_elem(&cgrp_node_stash, &cgid);
	scx_bpf_destroy_dsq(cgid);
}

void BPF_STRUCT_OPS(h_cgroup_move, struct task_struct *p,
		    struct cgroup *from, struct cgroup *to)
{
	struct cgrp_info *from_cgi, *to_cgi;
	// s64 delta;

	/* find_cgrp_ctx() triggers scx_ops_error() on lookup failures */
	if (!(from_cgi = find_cgrp_info(from)) || !(to_cgi = find_cgrp_info(to)))
		return;

	// delta = time_delta(p->scx.dsq_vtime, from_cgi->tvtime_now);
	// p->scx.dsq_vtime = to_cgi->tvtime_now + delta;
}



// =======================================================
// TASK BPF OPS
// =======================================================

/*
 * @dispatch: Dispatch tasks from the BPF scheduler and/or user DSQs
 * @cpu: CPU to dispatch tasks for
 * @prev: previous task being switched out
 *
 * Called when a CPU's local dsq is empty. The operation should dispatch
 * one or more tasks from the BPF scheduler into the DSQs using
 * scx_bpf_dsq_insert() and/or move from user DSQs into the local DSQ
 * using scx_bpf_dsq_move_to_local().
 *
 * The maximum number of times scx_bpf_dsq_insert() can be called
 * without an intervening scx_bpf_dsq_move_to_local() is specified by
 * ops.dispatch_max_batch. See the comments on top of the two functions
 * for more details.
 *
 * When not %NULL, @prev is an SCX task with its slice depleted. If
 * @prev is still runnable as indicated by set %SCX_TASK_QUEUED in
 * @prev->scx.flags, it is not enqueued yet and will be enqueued after
 * ops.dispatch() returns. To keep executing @prev, return without
 * dispatching or moving any tasks. Also see %SCX_OPS_ENQ_LAST.
 */

void BPF_STRUCT_OPS(h_dispatch, s32 cpu, struct task_struct *prev)
{
	struct core_info *ci;
	struct cgrp_info *cgi;
	struct cgroup *kernel_group;
	u64 now = scx_bpf_now();
	bool picked_next = false;

	ci = find_cpu_ctx();
	if (!ci)
		return;

	if (!ci->cur_cgid)
		goto pick_next_cgroup;

	if (time_before(now, ci->cur_at + cgrp_slice_ns)) {
		if (scx_bpf_dsq_move_to_local(ci->cur_cgid)) { // fails if old group empty
			if (cpu == 4) {
				bpf_printk("DISPATCH keeping same grp %d", ci->cur_cgid);
			}
			return;
		}
	}

	/*
	 * The current cgroup is expiring. It was already charged a full slice.
	 * Calculate the actual usage and accumulate the delta.
	 */
	kernel_group = bpf_cgroup_from_id(ci->cur_cgid);
	if (!kernel_group) {
		// cgroup no longer exists?
		goto pick_next_cgroup;
	}

	cgi = bpf_cgrp_storage_get(&cgrp_info_storage, kernel_group, 0, 0);
	if (cgi) {
		/*
		 * We want to update the vtime delta and then look for the next
		 * cgroup to execute but the latter needs to be done in a loop
		 * and we can't keep the lock held. Oh well...
		 */
		bpf_spin_lock(&cg_tree_lock);
		// time_left_to_account_for = time_got - time_expected = (now - cpuc->cur_at) - cgrp_slice_ns
		s64 time_expected = 0; // TODO: FOR NOW, later will be cgrp_slice_ns
		s64 time_gotten = (s64)now - (s64)ci->cur_at;
		s64 to_add = signed_div((time_gotten - time_expected) * (s64)FCG_HWEIGHT_ONE, (s64)(cgi->hweight ?: 1));
		s64 old_delta = __atomic_fetch_add(&cgi->svt_delta, to_add, __ATOMIC_ACQUIRE);
		s64 old_gl_svt = __atomic_add_fetch(&global_curr_sum_svt, to_add, __ATOMIC_ACQUIRE);
		bpf_spin_unlock(&cg_tree_lock);

		// TODO: 
		// TODO: big problem: if this decreases the time, that won't ever be applied to the tree!
		// TODO: 

		if (cpu == 4) {
			bpf_printk("%d ran (collapsed delta=%lld, old_delta=%lld, added %lld ((%llu - %llu - %llu) * 65536/%lu))", 
				ci->cur_cgid, cgi->svt_delta, old_delta, to_add, now, ci->cur_at, cgrp_slice_ns, cgi->hweight);
		}
	} // else cgroup no longer existss

	bpf_cgroup_release(kernel_group);

pick_next_cgroup:
	ci->cur_at = now;

	if (scx_bpf_dsq_move_to_local(TMP_GLOBAL_Q)) {
		if (cpu == 4) {
			bpf_printk("DISPATCH pulled from tmp global dsq");
		}
		ci->cur_cgid = 0;
		return;
	}

	// bpf_repeat(CGROUP_MAX_RETRIES) {
	// 	if (try_pick_next_cgroup(&ci->cur_cgid)) {
	// 		picked_next = true;
	// 		break;
	// 	}
	// }

	/*
	 * This only happens if try_pick_next_cgroup() races against enqueue
	 * path for more than CGROUP_MAX_RETRIES times, which is extremely
	 * unlikely and likely indicates an underlying bug. There shouldn't be
	 * any stall risk as the race is against enqueue.
	 */
}



/*
 * @enqueue: Enqueue a task on the BPF scheduler
 * @p: task being enqueued
 * @enq_flags: %SCX_ENQ_*
 *
 * @p is ready to run. Insert directly into a DSQ by calling
 * scx_bpf_dsq_insert() or enqueue on the BPF scheduler. If not directly
 * inserted, the bpf scheduler owns @p and if it fails to dispatch @p,
 * the task will stall.
 *
 * If @p was inserted into a DSQ from ops.select_cpu(), this callback is
 * skipped.
 */
void BPF_STRUCT_OPS(h_enqueue, struct task_struct *p, u64 enq_flags)
{
	struct cgroup *kernel_group;
	struct cgrp_info *cgi;

	kernel_group = __COMPAT_scx_bpf_task_cgroup(p);

	cgi = find_cgrp_info(kernel_group);
	if (!cgi) {
		goto out_release;
	}

	bpf_printk("ENQ %d to %d", p->pid, kernel_group->kn->id);

	scx_bpf_dsq_insert(p, kernel_group->kn->id, MY_SLICE, enq_flags);

	struct cgrp_node_stash *stash;
	struct cgrp_node *cg_node;
	u64 cgid = kernel_group->kn->id;

	// do some sort of sync w/ pick_min_group?

	// move node to tree (from the stash)
	// what happens if you add something that already exists?
	stash = bpf_map_lookup_elem(&cgrp_node_stash, &cgid);
	if (!stash) {
		scx_bpf_error("cgv_node lookup failed for cgid %llu", cgid);
		goto out_release;
	}
	cg_node = bpf_kptr_xchg(&stash->node, NULL);
	if (!cg_node) {
		// the node is already on the tree
		goto out_release;
	}
		
	u64 curr_svt = gl_avg_svt();

	bpf_spin_lock(&cg_tree_lock);
	cg_node->svt = curr_svt;
	bpf_rbtree_add(&cg_tree, &cg_node->rb_node, cg_node_less);
	bpf_spin_unlock(&cg_tree_lock);

	update_active_weight_sums(kernel_group, true);
	u64 new_num_grps = __atomic_add_fetch(&global_num_contending_grps, 1, __ATOMIC_ACQUIRE);
	bpf_printk("  new grp, svt=%llu, now have %d active grps", curr_svt, global_num_contending_grps);

out_release:
	bpf_cgroup_release(kernel_group);
	
}




s32 BPF_STRUCT_OPS_SLEEPABLE(h_init)
{
    return 0;
}

void BPF_STRUCT_OPS(h_exit, struct scx_exit_info *ei)
{
    UEI_RECORD(uei, ei);
}

SCX_OPS_DEFINE(h_ops,
        // .init_task		= (void *)h_init_task,
        // .select_cpu		= (void *)h_select_cpu,
        .enqueue		= (void *)h_enqueue,
        // .runnable		= (void *)h_runnable,
        // .dispatch		= (void *)h_dispatch,
        // .running		= (void *)h_running,
        .stopping		= (void *)h_stopping,
        // .quiescent		= (void *)h_quiescent,
        // .exit_task		= (void *)h_exit_task,
        // .enable			= (void *)h_enable,
        // .disable		= (void *)h_disable,
        .cgroup_init		= (void *)h_cgroup_init,
        .cgroup_set_weight	= (void *)h_cgroup_set_weight,
        .cgroup_move		= (void *)h_cgroup_move,
        .cgroup_exit        = (void *)h_cgroup_exit,
        .init			= (void *)h_init,
        .exit			= (void *)h_exit,
        .flags			= SCX_OPS_HAS_CGROUP_WEIGHT | SCX_OPS_ENQ_EXITING,
        .name			= "h");
