#ifndef __SCX_EXAMPLE_FLATCG_H
#define __SCX_EXAMPLE_FLATCG_H

enum {
	FCG_HWEIGHT_ONE		= 1LLU << 16,	/* Base weight unit (65536) for hierarchical weight calculations. Used as the denominator when computing proportional shares across the flattened cgroup hierarchy. */
};

enum fcg_stat_idx {
	FCG_STAT_ACT,		/* Cgroup became active (gained runnable tasks) */
	FCG_STAT_DEACT,		/* Cgroup became inactive (lost all runnable tasks) */
	FCG_STAT_LOCAL,		/* Task dispatched to local CPU (idle core) */
	FCG_STAT_GLOBAL,	/* Task dispatched to global fallback queue */

	FCG_STAT_HWT_UPDATES,	/* Hierarchical weight recalculations performed */
	FCG_STAT_HWT_CACHE,	/* Hierarchical weight used from cache (no recalculation needed) */
	FCG_STAT_HWT_SKIP,	/* Hierarchical weight update skipped (cgroup inactive) */
	FCG_STAT_HWT_RACE,	/* Race condition detected during hierarchical weight update */

	FCG_STAT_ENQ_SKIP,	/* Cgroup enqueue skipped (already queued) */
	FCG_STAT_ENQ_RACE,	/* Race condition during cgroup enqueue */

	FCG_STAT_CNS_KEEP,	/* Current cgroup slice continued (tasks available) */
	FCG_STAT_CNS_EXPIRE,	/* Current cgroup slice expired (time limit reached) */
	FCG_STAT_CNS_EMPTY,	/* Current cgroup slice ended (no more tasks) */
	FCG_STAT_CNS_GONE,	/* Current cgroup no longer exists */

	FCG_STAT_PNC_NO_CGRP,	/* No cgroup available for picking next */
	FCG_STAT_PNC_NEXT,	/* Successfully picked next cgroup */
	FCG_STAT_PNC_EMPTY,	/* Picked cgroup had no tasks */
	FCG_STAT_PNC_GONE,	/* Picked cgroup no longer exists */
	FCG_STAT_PNC_RACE,	/* Race condition during next cgroup pick */
	FCG_STAT_PNC_FAIL,	/* Failed to pick next cgroup after max retries */

	FCG_STAT_BAD_REMOVAL,	/* Invalid cgroup node removal from tree */

	FCG_NR_STATS,
};

struct fcg_cgrp_ctx {
	// nr of threads in the cycle (bookmarked by runnable and quescient) [TODO: given the below, not sure how it's ok to sometimes not update nr_active?]
	u32			nr_active; 
	//  In leaf cgroups, ->nr_runnable which is updated with __sync operations gates ->nr_active updates, so that we don't have to grab the cgv_tree_lock repeatedly for a busy cgroup which is staying active
	u32			nr_runnable; 
	// used to synchronize between pick_min_grp and enq -> former sets it to 0, latter to 1 (used nowhere else?)
	u32			queued; 
	// base weight of the group (unflattened!)
	u32			weight; 
	// effective/globalized weight of the group [Calculated by compounding at each level: hweight = parent_hweight * weight / parent_child_weight_sum]
	u32			hweight; 
	// Sum of weights of all active child cgroups. Used as denominator when calculating hierarchical weights for children. Updated when child cgroups become active/inactive.
	u64			child_weight_sum; 
	// Generation counter for hierarchical weight caching. Incremented when weight tree changes to invalidate cached hweight values. Prevents stale weight calculations.
	u64			hweight_gen; 
	// a cache for the svt updates to the group; synced to the groups tree node before the node is added back to the tree
	s64			cvtime_delta; 
	// tasks (max) vtime; used in cgroup_move, init_task
	u64			tvtime_now; 

};

#endif /* __SCX_EXAMPLE_FLATCG_H */
