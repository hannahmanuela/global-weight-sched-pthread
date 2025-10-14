/* SPDX-License-Identifier: GPL-2.0 */
/*
 * A hierarchical global accounting scheduler for sched_ext.
 *
 * This scheduler implements the global accounting logic from global-accounting-cas.c
 * in a BPF sched_ext program. It maintains global virtual time tracking and
 * supports cgroup hierarchies with proper weight-based scheduling.
 *
 * Key features:
 * - Global virtual time that progresses based on actual CPU usage
 * - Group-based scheduling with weight support
 * - Lag tracking for inactive groups
 * - Cgroup hierarchy support
 */

#include <scx/common.bpf.h>

char _license[] SEC("license") = "GPL";

UEI_DEFINE(uei);

// =======================================================
// DEFINES
// =======================================================

#define MY_SLICE ((__u64)4 * 1000000) // 4ms

// =======================================================
// DATA STRUCTURES
// =======================================================

struct cgroup_info {
    u64 spec_virt_time;    // speculative virtual time
    u64 virt_lag;          // lag when group becomes inactive
    u64 last_virt_time;    // last virtual time when group was active
    u32 num_threads;       // total number of threads in group
    u32 threads_queued;    // number of runnable threads
    u32 weight;            // group weight
    u32 queued;            // whether group is in the scheduling tree

    u32 group_dsq;
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
#define GLOBAL_ACTIVE_GROUPS_IDX 3 // num active groups

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

static u64 get_global_active_groups(void)
{
    u32 idx = GLOBAL_ACTIVE_GROUPS_IDX;
    u64 *groups = bpf_map_lookup_elem(&global_state, &idx);
    return groups ? *groups : 0;
}

static void set_global_active_groups(u64 groups)
{
    u32 idx = GLOBAL_ACTIVE_GROUPS_IDX;
    bpf_map_update_elem(&global_state, &idx, &groups, BPF_ANY);
}

static struct cgroup_info *get_cgroup_info(struct cgroup *cgrp)
{
    return bpf_cgrp_storage_get(&cgroup_info, cgrp, 0, 0);
}

static struct task_info *get_task_info(struct task_struct *p)
{
    return bpf_task_storage_get(&task_info, p, 0, 0);
}

// Calculate average virtual time across all active groups
static u64 get_average_vtime(struct cgroup *exclude_cgrp)
{
    u64 total_vtime = 0;
    u64 num_groups = 0;
    u64 active_groups = get_global_active_groups();
    
    // For simplicity, use the global virtual time as the average
    // In a more sophisticated implementation, we would iterate through
    // all active groups and calculate the true average
    if (active_groups > 0) {
        return get_global_vtime();
    }
    
    return 0;
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
    gi = bpf_cgrp_storage_get(&cgroup_info, cgrp, 0, 0);
    if (!gi)
        return -ENOMEM;
    
    // Set initial group values if this is the first task
    if (gi->num_threads == 0) {
        gi->spec_virt_time = global_vtime;
        gi->virt_lag = 0;
        gi->last_virt_time = global_vtime;
        gi->queued = 0;

        scx_bpf_create_dsq(gi->group_dsq, -1);
    }
    
    gi->num_threads++;

    bpf_printk("INIT_TASK Task %d, vtime=%llu cgrp %d weight %d", p->pid, global_vtime, cgrp->kn->id, p->scx.weight);
    
    // Update global total weight
    set_global_total_weight(get_global_total_weight() + p->scx.weight);
    
    return 0;
}

s32 BPF_STRUCT_OPS_SLEEPABLE(h_cgroup_init, struct cgroup *cgrp, struct scx_cgroup_init_args *args)
{
    struct cgroup_info *gi;
    
    bpf_printk("CGROUP_INIT Grp id %d, weight=%d", cgrp->kn->id, args->weight);
    gi = bpf_cgrp_storage_get(&cgroup_info, cgrp, 0, BPF_LOCAL_STORAGE_GET_F_CREATE);
    if (!gi)
        return -ENOMEM;
    
    gi->weight = args->weight;
    gi->spec_virt_time = 0;
    gi->virt_lag = 0;
    gi->last_virt_time = 0;
    gi->num_threads = 0;
    gi->threads_queued = 0;
    gi->queued = 0;
    
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

    bpf_printk("CGROUP_SET_WEIGHT Grp id %d, weight=%d", cgrp->kn->id, weight);
}

void BPF_STRUCT_OPS(h_cgroup_move, struct cgroup *from, struct cgroup *to)
{
    struct cgroup_info *from_gi, to_gi;
    from_gi = get_cgroup_info(from);
    to_gi = get_cgroup_info(to);
    if (!from_gi || !to_gi) {
        bpf_printk("ERROR: No group info for cgroup %d or %d?", from->kn->id, to->kn->id);
        return;
    }

    from_gi->num_threads -= 1;
    to_gi->num_threads += 1;
}

void BPF_STRUCT_OPS(h_enable, struct task_struct *p)
{
    struct task_info *ti = get_task_info(p);
    struct cgroup_info *gi;
    struct cgroup *cgrp = __COMPAT_scx_bpf_task_cgroup(p);
    u64 global_vtime = get_global_vtime();
    
    if (!ti) {
        bpf_printk("ERROR: No task info for task %d", p->pid);
        bpf_cgroup_release(cgrp);
        return;
    }
    
    gi = get_cgroup_info(cgrp);
    if (!gi) {
        bpf_printk("ERROR: No group info for task %d", p->pid);
        bpf_cgroup_release(cgrp);
        return;
    }
    
    // Set task vruntime to current global vtime
    ti->vruntime = global_vtime;
    ti->runnable = 1;
    
    // Update group queued count
    gi->threads_queued++;
    
    // If this is the first runnable task in the group, initialize group vtime
    if (gi->threads_queued == 1) {
        u64 initial_vtime = get_average_vtime(cgrp);
        
        // Handle lag from previous inactivity
        if (gi->virt_lag > 0) {
            if (gi->last_virt_time > initial_vtime) {
                initial_vtime = gi->last_virt_time;
            }
        } else if (gi->virt_lag < 0) {
            initial_vtime -= gi->virt_lag;
        }
        
        gi->spec_virt_time = initial_vtime;
        gi->queued = 1;
    }
    
    bpf_cgroup_release(cgrp);
}

void BPF_STRUCT_OPS(h_disable, struct task_struct *p)
{
    struct task_info *ti = get_task_info(p);
    struct cgroup_info *gi;
    struct cgroup *cgrp = __COMPAT_scx_bpf_task_cgroup(p);
    
    if (!ti || !cgrp) {
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
        gi->virt_lag = curr_avg_vtime - gi->spec_virt_time;
        gi->last_virt_time = gi->spec_virt_time;
        gi->queued = 0;
    }
    
    ti->runnable = 0;
    bpf_cgroup_release(cgrp);
}

s32 BPF_STRUCT_OPS(h_exit_task, struct task_struct *p)
{
    struct task_info *ti = get_task_info(p);
    struct cgroup_info *gi;
    struct cgroup *cgrp = __COMPAT_scx_bpf_task_cgroup(p);
    
    if (!ti || !cgrp) {
        if (cgrp) bpf_cgroup_release(cgrp);
        return 0;
    }
    
    gi = get_cgroup_info(cgrp);
    if (!gi) {
        bpf_cgroup_release(cgrp);
        return 0;
    }
    
    // Update group thread count
    if (gi->num_threads > 0)
        gi->num_threads--;
    
    // Update global total weight
    set_global_total_weight(get_global_total_weight() - p->scx.weight);
    
    bpf_cgroup_release(cgrp);
    return 0;
}

s32 BPF_STRUCT_OPS(h_select_cpu, struct task_struct *p, s32 prev_cpu, u64 wake_flags)
{
    bool is_idle = false;
    s32 cpu;
    
    cpu = scx_bpf_select_cpu_dfl(p, prev_cpu, wake_flags, &is_idle);
    if (is_idle) {
        scx_bpf_dsq_insert(p, SCX_DSQ_LOCAL, MY_SLICE, 0);
    }
    
    return cpu;
}

void BPF_STRUCT_OPS(h_enqueue, struct task_struct *p, u64 enq_flags)
{
    struct task_info *ti = get_task_info(p);
    struct cgroup_info *gi;
    struct cgroup *cgrp = __COMPAT_scx_bpf_task_cgroup(p);
    
    if (!ti || !cgrp) {
        if (cgrp) bpf_cgroup_release(cgrp);
        return;
    }
    
    gi = get_cgroup_info(cgrp);
    if (!gi) {
        bpf_cgroup_release(cgrp);
        return;
    }
    
    // Use group's speculative virtual time for enqueueing
    u64 vtime = gi->spec_virt_time;
    
    // Limit budget accumulation
    u64 global_vtime = get_global_vtime();
    if (time_before(vtime, global_vtime - MY_SLICE))
        vtime = global_vtime - MY_SLICE;
    
    scx_bpf_dsq_insert_vtime(p, SHARED_DSQ, MY_SLICE, vtime, enq_flags);
    
    bpf_cgroup_release(cgrp);
}

void BPF_STRUCT_OPS(h_dispatch, s32 cpu, struct task_struct *prev)
{
    // Move a task from shared DSQ to local DSQ
    scx_bpf_dsq_move_to_local(SHARED_DSQ);
}

void BPF_STRUCT_OPS(h_running, struct task_struct *p)
{
    struct task_info *ti = get_task_info(p);
    struct cgroup_info *gi;
    struct cgroup *cgrp = __COMPAT_scx_bpf_task_cgroup(p);
    
    if (!ti || !cgrp) {
        if (cgrp) bpf_cgroup_release(cgrp);
        return;
    }
    
    gi = get_cgroup_info(cgrp);
    if (!gi) {
        bpf_cgroup_release(cgrp);
        return;
    }

    bpf_printk("RUNNING Task %d, vtime=%llu cgrp %d cgrp weight %d p weight %d", p->pid, p->scx.dsq_vtime, 
        cgrp->kn->id, gi->weight, p->scx.weight);
    
    // Update global virtual time to match the running task
    u64 global_vtime = get_global_vtime();
    if (time_before(global_vtime, p->scx.dsq_vtime))
        set_global_vtime(p->scx.dsq_vtime);
    
    bpf_cgroup_release(cgrp);
}

void BPF_STRUCT_OPS(h_stopping, struct task_struct *p, bool runnable)
{
    struct task_info *ti = get_task_info(p);
    struct cgroup_info *gi;
    struct cgroup *cgrp = __COMPAT_scx_bpf_task_cgroup(p);
    
    if (!ti || !cgrp) {
        if (cgrp) bpf_cgroup_release(cgrp);
        return;
    }
    
    gi = get_cgroup_info(cgrp);
    if (!gi) {
        bpf_cgroup_release(cgrp);
        return;
    }
    
    // Calculate actual execution time and update virtual time
    u64 exec_time = MY_SLICE - p->scx.slice;
    u64 weighted_exec_time = safe_div_u64(exec_time, p->scx.weight);
    
    // Update task vruntime
    ti->vruntime += weighted_exec_time;
    p->scx.dsq_vtime += weighted_exec_time;
    
    // Update group speculative virtual time
    u64 group_weighted_time = safe_div_u64(exec_time, gi->weight);
    gi->spec_virt_time += group_weighted_time;
    
    // Update global virtual time
    u64 global_weighted_time = safe_div_u64(exec_time, get_global_total_weight());
    set_global_vtime(get_global_vtime() + global_weighted_time);
    
    bpf_cgroup_release(cgrp);
}

void BPF_STRUCT_OPS(h_quiescent, struct task_struct *p, u64 deq_flags)
{
    struct task_info *ti = get_task_info(p);
    struct cgroup_info *gi;
    struct cgroup *cgrp = __COMPAT_scx_bpf_task_cgroup(p);
    
    if (!ti || !cgrp) {
        if (cgrp) bpf_cgroup_release(cgrp);
        return;
    }
    
    gi = get_cgroup_info(cgrp);
    if (!gi) {
        bpf_cgroup_release(cgrp);
        return;
    }
    
    // Calculate task lag
    u64 global_vtime = get_global_vtime();
    u64 task_lag = p->scx.dsq_vtime - global_vtime;
    
    ti->vlag = task_lag;
    ti->last_vruntime = p->scx.dsq_vtime;
    ti->runnable = 0;
    
    // Update group queued count
    if (gi->threads_queued > 0)
        gi->threads_queued--;
    
    // If no more runnable tasks in group, calculate group lag
    if (gi->threads_queued == 0) {
        u64 curr_avg_vtime = get_average_vtime(NULL);
        gi->virt_lag = curr_avg_vtime - gi->spec_virt_time;
        gi->last_virt_time = gi->spec_virt_time;
        gi->queued = 0;
    }
    
    bpf_cgroup_release(cgrp);
}

s32 BPF_STRUCT_OPS_SLEEPABLE(h_init)
{
    return scx_bpf_create_dsq(SHARED_DSQ, -1);
}

void BPF_STRUCT_OPS(h_exit, struct scx_exit_info *ei)
{
    UEI_RECORD(uei, ei);
}

SCX_OPS_DEFINE(h_ops,
        .init_task		= (void *)h_init_task,
        .select_cpu		= (void *)h_select_cpu,
        .enqueue		= (void *)h_enqueue,
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
