#include <scx/common.bpf.h>

char _license[] SEC("license") = "GPL";

UEI_DEFINE(uei);

// =======================================================
// DEFINES
// =======================================================

// #define SHARED_DSQ 0
#define MAX_NUM_GRPS 500
#define MY_SLICE ((__u64)4 * 1000000) // 4ms
#define INACTIVE_GROUP_INDEX 0xFFFFFFFF // indicates group is not in active list

// =======================================================
// DATA STRUCTURES
// =======================================================

struct cgroup_info {
    u32 group_id;          // group id
    u64 spec_virt_time;    // speculative virtual time
    // u64 virt_lag;          // lag when group becomes inactive
    // u64 last_virt_time;    // last virtual time when group was active
    u32 num_threads;       // total number of threads in group
    u32 threads_queued;    // number of runnable threads
    u32 weight;            // group weight
    u32 active_group_index; // index in active_groups array (0xFFFFFFFF if not active)

    struct cgroup_info *next;
};

/* Lightweight struct for active groups list */
struct active_group {
    u32 group_id;
    u64 spec_virt_time;
};

// =======================================================
// MAPS
// =======================================================

/* Map of cgroup groups */
struct {
    __uint(type, BPF_MAP_TYPE_CGRP_STORAGE);
    __uint(map_flags, BPF_F_NO_PREALLOC);
    __type(key, int);
    __type(value, struct cgroup_info);
} cgroup_info SEC(".maps");

/* Global state */
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(key_size, sizeof(u32));
    __uint(value_size, sizeof(u64));
    __uint(max_entries, 4);
} global_state SEC(".maps");

#define GLOBAL_VTIME_IDX 0 // global virtual time
#define GLOBAL_MIN_VTIME_IDX 1 // minimum virtual time
#define GLOBAL_TOTAL_WEIGHT_IDX 2 // total weight (sum of groups)
#define GLOBAL_ACTIVE_GROUPS_COUNT_IDX 3 // number of active groups

/* Active groups list - using array map instead of linked list */
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(key_size, sizeof(u32));
    __uint(value_size, sizeof(struct active_group)); // stores group_id and spec_virt_time
    __uint(max_entries, MAX_NUM_GRPS);
} active_groups SEC(".maps");

/* Note: We removed the spin lock since BPF map operations are atomic */

// =======================================================
// HELPER FUNCTIONS
// =======================================================

static __always_inline __u64 safe_div_u64(__u64 a, __u64 b)
{
    if (b == 0)
        return (u64)-1;
    return a / b;
}

static u64 get_global_vtime(void)
{
    u32 idx = GLOBAL_VTIME_IDX;
    u64 *vtime = bpf_map_lookup_elem(&global_state, &idx);
    return vtime ? *vtime : 0;
}

static void set_global_vtime(u64 vtime)
{
    u32 idx = GLOBAL_VTIME_IDX;
    bpf_map_update_elem(&global_state, &idx, &vtime, BPF_ANY);
}

static u64 get_global_min_vtime(void)
{
    u32 idx = GLOBAL_MIN_VTIME_IDX;
    u64 *min_vtime = bpf_map_lookup_elem(&global_state, &idx);
    return min_vtime ? *min_vtime : 0;
}

static void set_global_min_vtime(u64 min_vtime)
{
    u32 idx = GLOBAL_MIN_VTIME_IDX;
    bpf_map_update_elem(&global_state, &idx, &min_vtime, BPF_ANY);
}

static u64 get_global_total_weight(void)
{
    u32 idx = GLOBAL_TOTAL_WEIGHT_IDX;
    u64 *weight = bpf_map_lookup_elem(&global_state, &idx);
    return weight ? *weight : 0;
}

static void set_global_total_weight(u64 weight)
{
    u32 idx = GLOBAL_TOTAL_WEIGHT_IDX;
    bpf_map_update_elem(&global_state, &idx, &weight, BPF_ANY);
}

static u64 get_active_groups_count(void)
{
    u32 idx = GLOBAL_ACTIVE_GROUPS_COUNT_IDX;
    u64 *count = bpf_map_lookup_elem(&global_state, &idx);
    return count ? *count : 0;
}

static void set_active_groups_count(u64 count)
{
    u32 idx = GLOBAL_ACTIVE_GROUPS_COUNT_IDX;
    bpf_map_update_elem(&global_state, &idx, &count, BPF_ANY);
}

/* Helper functions for active groups management */

static int add_active_group(u32 group_id, u64 spec_virt_time, struct cgroup_info *gi)
{
    u64 current_count = get_active_groups_count();
    
    // Check if we have space
    if (current_count >= MAX_NUM_GRPS) {
        return -1; // No space
    }
    
    // Create the active group struct
    struct active_group ag = {
        .group_id = group_id,
        .spec_virt_time = spec_virt_time
    };
    
    // Insert at the current count index
    u32 index = (u32)current_count;
    if (bpf_map_update_elem(&active_groups, &index, &ag, BPF_ANY) != 0) {
        return -1; // Failed to insert
    }
    
    // Set the back pointer in cgroup_info
    if (gi) {
        gi->active_group_index = index;
    }
    
    // Increment the count atomically
    set_active_groups_count(current_count + 1);
    
    return 0; // Successfully added
}

static int remove_active_group(u32 group_id, struct cgroup_info *gi)
{
    u64 current_count = get_active_groups_count();
    
    if (current_count == 0) {
        return -1; // No groups to remove
    }
    
    u32 index_to_remove;
    
    if (gi && gi->active_group_index != INACTIVE_GROUP_INDEX) {
        // Use the back pointer for O(1) access
        index_to_remove = gi->active_group_index;
        
        // Verify the group_id matches (safety check)
        struct active_group *ag = bpf_map_lookup_elem(&active_groups, &index_to_remove);
        if (!ag || ag->group_id != group_id) {
            return -1; // Back pointer is stale or invalid
        }
    } else {
        // Fallback: search for the group (shouldn't happen in normal operation)
        for (u32 i = 0; i < current_count; i++) {
            struct active_group *existing_ag = bpf_map_lookup_elem(&active_groups, &i);
            if (existing_ag && existing_ag->group_id == group_id) {
                index_to_remove = i;
                break;
            }
        }
        return -1; // Not found
    }
    
    // Remove by swapping with the last element
    if (current_count > 1) {
        u32 last_index = (u32)(current_count - 1);
        struct active_group *last_ag = bpf_map_lookup_elem(&active_groups, &last_index);
        if (last_ag) {
            // Move last element to the position we're removing
            bpf_map_update_elem(&active_groups, &index_to_remove, last_ag, BPF_ANY);
            
            // Update the back pointer of the moved group
            // We need to find the cgroup_info for the moved group
            // This is a limitation - we'd need the cgroup to update its back pointer
        }
    }
    
    // Clear the last position
    struct active_group zero_ag = {0};
    u32 last_index = (u32)(current_count - 1);
    bpf_map_update_elem(&active_groups, &last_index, &zero_ag, BPF_ANY);
    
    // Clear the back pointer in cgroup_info
    if (gi) {
        gi->active_group_index = INACTIVE_GROUP_INDEX;
    }
    
    // Decrement the count
    set_active_groups_count(current_count - 1);
    
    return 0; // Successfully removed
}

static int update_active_group_spec_virt_time(u32 group_id, u64 new_spec_virt_time, struct cgroup_info *gi)
{
    if (!gi || gi->active_group_index == INACTIVE_GROUP_INDEX) {
        return -1; // Group is not active
    }
    
    u32 index = gi->active_group_index;
    struct active_group *ag = bpf_map_lookup_elem(&active_groups, &index);
    if (!ag || ag->group_id != group_id) {
        return -1; // Back pointer is stale or invalid
    }
    
    // Update the spec_virt_time
    ag->spec_virt_time = new_spec_virt_time;
    bpf_map_update_elem(&active_groups, &index, ag, BPF_ANY);
    
    return 0; // Successfully updated
}

static struct cgroup_info *get_cgroup_info(struct cgroup *cgrp)
{
    return bpf_cgrp_storage_get(&cgroup_info, cgrp, 0, 0);
}


// Calculate average virtual time across all active groups
static u64 get_average_vtime(struct cgroup *exclude_cgrp)
{
    // For simplicity, use the global virtual time as the average
    // In a more sophisticated implementation, we would iterate through
    // all active groups and calculate the true average
    return get_global_vtime();
}

// =======================================================
// BPF OPS
// =======================================================

s32 BPF_STRUCT_OPS(h_init_task, struct task_struct *p, struct scx_init_task_args *args)
{
    struct cgroup_info *gi;
    struct cgroup *cgrp = args->cgroup;
    u64 global_vtime = get_global_vtime();
    
    // group should already exist
    gi = get_cgroup_info(cgrp);
    if (!gi)
        return -1;
    
    return 0;
}

s32 BPF_STRUCT_OPS_SLEEPABLE(h_cgroup_init, struct cgroup *cgrp, struct scx_cgroup_init_args *args)
{
    struct cgroup_info *gi;
    
    gi = bpf_cgrp_storage_get(&cgroup_info, cgrp, 0, BPF_LOCAL_STORAGE_GET_F_CREATE);
    if (!gi)
        return -ENOMEM;
    
    u64 global_vtime = get_global_vtime();

    // everything else is initialized to 0
    gi->group_id = cgrp->kn->id;
    gi->weight = args->weight;
    gi->spec_virt_time = global_vtime;
    gi->active_group_index = INACTIVE_GROUP_INDEX; // initially inactive
    int ret = scx_bpf_create_dsq(cgrp->kn->id, -1);
    if (ret)
        return ret;

    // Add to active groups list
    add_active_group(cgrp->kn->id, gi->spec_virt_time, gi);
    
    return 0;
}

void BPF_STRUCT_OPS(h_cgroup_set_weight, struct cgroup *cgrp, u32 weight)
{
    struct cgroup_info *gi;
    gi = get_cgroup_info(cgrp);
    if (!gi) {
        bpf_printk("ERROR: No group info for cgroup %d?", cgrp->kn->id);
        return;
    }

    gi->weight = weight;
}

void BPF_STRUCT_OPS(h_cgroup_move, struct task_struct *p, struct cgroup *from, struct cgroup *to)
{
    struct cgroup_info *from_gi, *to_gi;
    from_gi = get_cgroup_info(from);
    to_gi = get_cgroup_info(to);
    if (!from_gi || !to_gi) {
        bpf_printk("ERROR: No group info for cgroup %d or %d?", from->kn->id, to->kn->id);
        return;
    }

    from_gi->num_threads -= 1;
    to_gi->num_threads += 1;

    // move the task to the new group's dsq, if it's in the old group's dsq
    struct task_struct *curr_task;
    bpf_for_each(scx_dsq, curr_task, from_gi->group_id, 0) {
        if (curr_task->pid == p->pid) {
            // move it to the to group
            scx_bpf_dsq_move(BPF_FOR_EACH_ITER, curr_task, to_gi->group_id, 0);
            break;
        }
    }
}

void BPF_STRUCT_OPS(h_enqueue, struct task_struct *p, u64 enq_flags)
{
    struct cgroup_info *gi;
    struct cgroup *cgrp = scx_bpf_task_cgroup(p);
    
    if (!cgrp) {
        if (cgrp) bpf_cgroup_release(cgrp);
        return;
    }
    
    gi = get_cgroup_info(cgrp);
    if (!gi) {
        bpf_cgroup_release(cgrp);
        return;
    }

    // set the cgroups' vtime if the threads_queued was 0?
    if (gi->threads_queued == 0) {
        u64 initial_vtime = get_average_vtime(cgrp);
        gi->spec_virt_time = initial_vtime;
        
        // Update the active groups list with new spec_virt_time
        update_active_group_spec_virt_time(cgrp->kn->id, initial_vtime, gi);

        set_global_total_weight(get_global_total_weight() + gi->weight);
    }
    
    gi->threads_queued++;
    scx_bpf_dsq_insert(p, gi->group_id, MY_SLICE, enq_flags);

    bpf_printk("ENQ Task %d, vtime=%llu cgrp %d cgrp weight %d", p->pid, p->scx.dsq_vtime, 
        cgrp->kn->id, gi->weight);
    
    bpf_cgroup_release(cgrp);
}

void BPF_STRUCT_OPS(h_quiescent, struct task_struct *p, u64 deq_flags)
{
    struct cgroup_info *gi;
    struct cgroup *cgrp = scx_bpf_task_cgroup(p);
    if (!cgrp) {
        if (cgrp) bpf_cgroup_release(cgrp);
        return;
    }
    gi = get_cgroup_info(cgrp);
    if (!gi) {
        bpf_cgroup_release(cgrp);
        return;
    }
    
    // Calculate task lag
    // u64 global_vtime = get_global_vtime();
    // u64 task_lag = p->scx.dsq_vtime - global_vtime;
    // ti->vlag = task_lag;
    // ti->last_vruntime = p->scx.dsq_vtime;
    // ti->runnable = 0;
    
    // Update group queued count
    if (gi->threads_queued > 0) {
        gi->threads_queued--;
    } else {
        bpf_printk("ERROR: threads_queued is 0 but now task %d is blocking cgrp %d\n", p->pid, cgrp->kn->id);
    }


    // If no more runnable tasks in group, calculate group lag
    if (gi->threads_queued == 0) {
        bpf_printk("QUIESC Task %d, cgpr weight %d, last task", p->pid, gi->weight);
        u64 curr_avg_vtime = get_average_vtime(NULL);
        // gi->virt_lag = curr_avg_vtime - gi->spec_virt_time;
        // gi->last_virt_time = gi->spec_virt_time;

        set_global_total_weight(get_global_total_weight() - gi->weight);
    } else {
        bpf_printk("QUIESC Task %d, cgpr weight %d, not last task", p->pid, gi->weight);
    }
    
    bpf_cgroup_release(cgrp);
}

static int pick_min_group() {
    int curr_min_grp = -1;
    u64 curr_min_svt = ~((u64)0); // max int

    u64 active_count = get_active_groups_count();
    
    // Iterate through active groups to find minimum
    for (u32 i = 0; i < MAX_NUM_GRPS; i++) {
        struct active_group *ag = bpf_map_lookup_elem(&active_groups, &i);
        if (!ag || i > active_count) {
            break;
        }
        
        // Check if this group has the minimum spec_virt_time
        if (ag->spec_virt_time < curr_min_svt) {
            curr_min_grp = ag->group_id;
            curr_min_svt = ag->spec_virt_time;
        }
        
        bpf_printk("Active group: %d, spec_virt_time: %llu\n", ag->group_id, ag->spec_virt_time);
    }
    
    return curr_min_grp;
}


void BPF_STRUCT_OPS(h_dispatch, s32 cpu, struct task_struct *prev)
{

    int min_group = pick_min_group();
    
    if (min_group < 0) {
        // go idle
        return;
    }

    // Move a task from shared DSQ to local DSQ
    scx_bpf_dsq_move_to_local(min_group);
}

// ------------------------------------------------------------
// line of things I've looked at 

void BPF_STRUCT_OPS(h_enable, struct task_struct *p)
{
    struct cgroup_info *gi;
    struct cgroup *cgrp = __COMPAT_scx_bpf_task_cgroup(p);
    u64 global_vtime = get_global_vtime();
    
    gi = get_cgroup_info(cgrp);
    if (!gi) {
        bpf_printk("ERROR: No group info for task %d", p->pid);
        bpf_cgroup_release(cgrp);
        return;
    }
    
    // Update group queued count
    gi->threads_queued++;
    
    // If this is the first runnable task in the group, initialize group vtime
    if (gi->threads_queued == 1) {
        u64 initial_vtime = get_average_vtime(cgrp);
        
        // Handle lag from previous inactivity
        // if (gi->virt_lag > 0) {
        //     if (gi->last_virt_time > initial_vtime) {
        //         initial_vtime = gi->last_virt_time;
        //     }
        // } else if (gi->virt_lag < 0) {
        //     initial_vtime -= gi->virt_lag;
        // }
        
        gi->spec_virt_time = initial_vtime;
        
        // Update the active groups list with new spec_virt_time
        update_active_group_spec_virt_time(cgrp->kn->id, initial_vtime, gi);
    }
    
    bpf_cgroup_release(cgrp);
}

void BPF_STRUCT_OPS(h_disable, struct task_struct *p)
{
    struct cgroup_info *gi;
    struct cgroup *cgrp = __COMPAT_scx_bpf_task_cgroup(p);
    
    if (!cgrp) {
        if (cgrp) bpf_cgroup_release(cgrp);
        return;
    }
    
    gi = get_cgroup_info(cgrp);
    if (!gi) {
        bpf_cgroup_release(cgrp);
        return;
    }
    
    // Update group queued count
    if (gi->threads_queued > 0)
        gi->threads_queued--;
    
    // If no more runnable tasks in group, calculate lag
    if (gi->threads_queued == 0) {
        u64 curr_avg_vtime = get_average_vtime(NULL);
        // gi->virt_lag = curr_avg_vtime - gi->spec_virt_time;
        // gi->last_virt_time = gi->spec_virt_time;
    }
    
    bpf_cgroup_release(cgrp);
}

s32 BPF_STRUCT_OPS(h_exit_task, struct task_struct *p)
{
    struct cgroup_info *gi;
    struct cgroup *cgrp = __COMPAT_scx_bpf_task_cgroup(p);
    
    if (!cgrp) {
        if (cgrp) bpf_cgroup_release(cgrp);
        return 0;
    }
    
    gi = get_cgroup_info(cgrp);
    if (!gi) {
        bpf_cgroup_release(cgrp);
        return 0;
    }
    
    // Update group thread count
    if (gi->num_threads > 0) {
        gi->num_threads--;
    } else {
        bpf_printk("ERROR: num_threads is 0 but now task %d is exiting cgrp %d\n", p->pid, cgrp->kn->id);
    }
    
    // Update global total weight
    if (gi->num_threads == 0) {
        set_global_total_weight(get_global_total_weight() - gi->weight);
    }
    
    bpf_cgroup_release(cgrp);
    return 0;
}

s32 BPF_STRUCT_OPS(h_select_cpu, struct task_struct *p, s32 prev_cpu, u64 wake_flags)
{
    bool is_idle = false;
    s32 cpu;
    
    cpu = scx_bpf_select_cpu_dfl(p, prev_cpu, wake_flags, &is_idle);
    // Note: scx_bpf_dsq_insert should not be called from select_cpu callback
    // The task will be enqueued via the enqueue callback
    
    return cpu;
}

void BPF_STRUCT_OPS(h_running, struct task_struct *p)
{
    struct cgroup_info *gi;
    struct cgroup *cgrp = __COMPAT_scx_bpf_task_cgroup(p);
    
    if (!cgrp) {
        if (cgrp) bpf_cgroup_release(cgrp);
        return;
    }
    
    gi = get_cgroup_info(cgrp);
    if (!gi) {
        bpf_cgroup_release(cgrp);
        return;
    }

    bpf_printk("RUNNING Task %d, vtime=%llu cgrp %d cgrp weight %d", p->pid, p->scx.dsq_vtime, 
        cgrp->kn->id, gi->weight);
    
    // Update global virtual time to match the running task
    u64 global_vtime = get_global_vtime();
    if (time_before(global_vtime, p->scx.dsq_vtime))
        set_global_vtime(p->scx.dsq_vtime);
    
    bpf_cgroup_release(cgrp);
}

void BPF_STRUCT_OPS(h_stopping, struct task_struct *p, bool runnable)
{
    struct cgroup_info *gi;
    struct cgroup *cgrp = __COMPAT_scx_bpf_task_cgroup(p);
    
    if (!cgrp) {
        if (cgrp) bpf_cgroup_release(cgrp);
        return;
    }
    
    gi = get_cgroup_info(cgrp);
    if (!gi) {
        bpf_cgroup_release(cgrp);
        return;
    }
    
    // Update group speculative virtual time
    // TODO: make this collapse rather than add
    u64 time_left = p->scx.slice;
    u64 group_weighted_time = safe_div_u64(time_left, gi->weight);
    gi->spec_virt_time -= group_weighted_time;
    
    // Update the active groups list with new spec_virt_time
    update_active_group_spec_virt_time(cgrp->kn->id, gi->spec_virt_time, gi);
    
    // Update global virtual time
    u64 exec_time = MY_SLICE - p->scx.slice;
    u64 global_weighted_time = safe_div_u64(exec_time, get_global_total_weight());
    set_global_vtime(get_global_vtime() + global_weighted_time);
    
    bpf_cgroup_release(cgrp);
}

s32 BPF_STRUCT_OPS_SLEEPABLE(h_init)
{
    // Initialize active groups map (all entries start as 0)
    // No explicit initialization needed for array maps
    return 0;
}

void BPF_STRUCT_OPS(h_exit, struct scx_exit_info *ei)
{
    UEI_RECORD(uei, ei);
}

SCX_OPS_DEFINE(h_ops,
        .init_task		= (void *)h_init_task,
        .select_cpu		= (void *)h_select_cpu,
        .enqueue		= (void *)h_enqueue,
        // .runnable		= (void *)h_runnable,
        .dispatch		= (void *)h_dispatch,
        .running		= (void *)h_running,
        .stopping		= (void *)h_stopping,
        .quiescent		= (void *)h_quiescent,
        .exit_task		= (void *)h_exit_task,
        .enable			= (void *)h_enable,
        .disable		= (void *)h_disable,
        .cgroup_init		= (void *)h_cgroup_init,
        .cgroup_set_weight	= (void *)h_cgroup_set_weight,
        .cgroup_move		= (void *)h_cgroup_move,
        .init			= (void *)h_init,
        .exit			= (void *)h_exit,
        .flags			= SCX_OPS_HAS_CGROUP_WEIGHT | SCX_OPS_ENQ_EXITING,
        .name			= "h");
