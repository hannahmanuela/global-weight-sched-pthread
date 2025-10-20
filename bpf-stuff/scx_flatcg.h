#ifndef __SCX_EXAMPLE_FLATCG_H
#define __SCX_EXAMPLE_FLATCG_H

enum {
	FCG_HWEIGHT_ONE		= 1LLU << 16,
};

enum fcg_stat_idx {
	FCG_STAT_ACT,
	FCG_STAT_DEACT,
	FCG_STAT_LOCAL,
	FCG_STAT_GLOBAL,

	FCG_STAT_HWT_UPDATES,
	FCG_STAT_HWT_CACHE,
	FCG_STAT_HWT_SKIP,
	FCG_STAT_HWT_RACE,

	FCG_STAT_ENQ_SKIP,
	FCG_STAT_ENQ_RACE,

	FCG_STAT_CNS_KEEP,
	FCG_STAT_CNS_EXPIRE,
	FCG_STAT_CNS_EMPTY,
	FCG_STAT_CNS_GONE,

	FCG_STAT_PNC_NO_CGRP,
	FCG_STAT_PNC_NEXT,
	FCG_STAT_PNC_EMPTY,
	FCG_STAT_PNC_GONE,
	FCG_STAT_PNC_RACE,
	FCG_STAT_PNC_FAIL,

	FCG_STAT_BAD_REMOVAL,

	FCG_NR_STATS,
};

struct fcg_cgrp_ctx {
	u32			nr_active; // nr of threads in the cycle (bookmarked by runnable and quescient) [TODO: given the below, not sure how it's ok to sometimes not update nr_active?]
	u32			nr_runnable; //  In leaf cgroups, ->nr_runnable which is updated with __sync operations gates ->nr_active updates, so that we don't have to grab the cgv_tree_lock repeatedly for a busy cgroup which is staying active
	u32			queued; // used to synchronize between pick_min_grp and enq -> former sets it to 0, latter to 1 (used nowhere else?)
	u32			weight; // the weight of the group (flattened/globalized)
	u32			hweight; // not sure something used to generate the correct globalized weights
	u64			child_weight_sum; // not sure something used to generate the correct globalized weights
	u64			hweight_gen; // not sure something used to generate the correct globalized weights
	s64			cvtime_delta; // a cache for the svt updates to the group? synced to the groups tree node before the node is added back to the tree
	u64			tvtime_now; // tasks (max) vtime? used in cgroup_move, init_task
	u64 		cvtime_copy; // a copy of the cvtime the group currently has in the tree
};

#endif /* __SCX_EXAMPLE_FLATCG_H */
