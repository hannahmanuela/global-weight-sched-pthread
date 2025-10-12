/* SPDX-License-Identifier: GPL-2.0 */
/*
* A simple scheduler.
*
* By default, it operates as a simple global weighted vtime scheduler and can
* be switched to FIFO scheduling. It also demonstrates the following niceties.
*
* - Statistics tracking how many tasks are queued to local and global dsq's.
* - Termination notification for userspace.
*
* While very simple, this scheduler should work reasonably well on CPUs with a
* uniform L3 cache topology. While preemption is not implemented, the fact that
* the scheduling queue is shared across all CPUs means that whatever is at the
* front of the queue is likely to be executed fairly quickly given enough
* number of CPUs. The FIFO scheduling mode may be beneficial to some workloads
* but comes with the usual problems with FIFO scheduling where saturating
* threads can easily drown out interactive ones.
*
* Copyright (c) 2022 Meta Platforms, Inc. and affiliates.
* Copyright (c) 2022 Tejun Heo <tj@kernel.org>
* Copyright (c) 2022 David Vernet <dvernet@meta.com>
*/
#include <scx/common.bpf.h>

char _license[] SEC("license") = "GPL";

const volatile bool fifo_sched;

static u64 vtime_now;
UEI_DEFINE(uei);

/*
* Built-in DSQs such as SCX_DSQ_GLOBAL cannot be used as priority queues
* (meaning, cannot be dispatched to with scx_bpf_dsq_insert_vtime()). We
* therefore create a separate DSQ with ID 0 that we dispatch to and consume
* from. If scx_simple only supported global FIFO scheduling, then we could just
* use SCX_DSQ_GLOBAL.
*/
#define SHARED_DSQ 0

#define MY_SLICE 4 *1000000 // 4ms

struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(key_size, sizeof(u32));
    __uint(value_size, sizeof(u64));
    __uint(max_entries, 2);			/* [local, global] */
} stats SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(key_size, sizeof(u32));
    __uint(value_size, sizeof(u64));
    __uint(max_entries, 3);			/* [select_cpu, enqueue, dispatch] */
} max_runtimes SEC(".maps");

/* Map to track weighted vruntime per process */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(key_size, sizeof(u32));		/* pid */
    __uint(value_size, sizeof(u64));		/* weighted vruntime */
    __uint(max_entries, 10000);
} process_vruntime SEC(".maps");

/* Map to track total weighted vruntime and process count for average calculation */
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(key_size, sizeof(u32));
    __uint(value_size, sizeof(u64));
    __uint(max_entries, 2);			/* [total_weighted_vruntime, process_count] */
} vruntime_stats SEC(".maps");

static void stat_inc(u32 idx)
{
    u64 *cnt_p = bpf_map_lookup_elem(&stats, &idx);
    if (cnt_p)
        (*cnt_p)++;
}

static void max_runtime_add(u32 idx, u64 time)
{
    u64 *cnt_p = bpf_map_lookup_elem(&max_runtimes, &idx);
    if (cnt_p && time > (*cnt_p))
        (*cnt_p) = time;
}

/* Helper function to update process vruntime and maintain average */
static void update_process_vruntime(u32 pid, u64 weighted_vruntime)
{
    u64 *existing_vruntime = bpf_map_lookup_elem(&process_vruntime, &pid);
    u32 total_idx = 0, count_idx = 1;
    u64 *total_vruntime, *process_count;
    
    /* Get current totals */
    total_vruntime = bpf_map_lookup_elem(&vruntime_stats, &total_idx);
    process_count = bpf_map_lookup_elem(&vruntime_stats, &count_idx);
    
    if (!total_vruntime || !process_count)
        return;
    
    /* If this is a new process, increment count */
    if (!existing_vruntime) {
        (*process_count)++;
    } else {
        /* Remove old vruntime from total */
        (*total_vruntime) -= *existing_vruntime;
    }
    
    /* Add new vruntime to total */
    (*total_vruntime) += weighted_vruntime;
    
    /* Update process vruntime */
    bpf_map_update_elem(&process_vruntime, &pid, &weighted_vruntime, BPF_ANY);
}

/* Helper function to get average vruntime */
static u64 get_average_vruntime(void)
{
    u32 total_idx = 0, count_idx = 1;
    u64 *total_vruntime, *process_count;
    
    total_vruntime = bpf_map_lookup_elem(&vruntime_stats, &total_idx);
    process_count = bpf_map_lookup_elem(&vruntime_stats, &count_idx);
    
    if (!total_vruntime || !process_count || *process_count == 0)
        return vtime_now; /* Fallback to current vtime */
    
    return *total_vruntime / *process_count;
}

/* Helper function to remove process from tracking */
static void remove_process_vruntime(u32 pid)
{
    u64 *existing_vruntime = bpf_map_lookup_elem(&process_vruntime, &pid);
    u32 total_idx = 0, count_idx = 1;
    u64 *total_vruntime, *process_count;
    
    if (!existing_vruntime)
        return;
    
    total_vruntime = bpf_map_lookup_elem(&vruntime_stats, &total_idx);
    process_count = bpf_map_lookup_elem(&vruntime_stats, &count_idx);
    
    if (!total_vruntime || !process_count)
        return;
    
    /* Remove from total and decrement count */
    (*total_vruntime) -= *existing_vruntime;
    if (*process_count > 0)
        (*process_count)--;
    
    /* Remove from process map */
    bpf_map_delete_elem(&process_vruntime, &pid);
}


s32 BPF_STRUCT_OPS(simple_select_cpu, struct task_struct *p, s32 prev_cpu, u64 wake_flags)
{
    u64 start = bpf_ktime_get_ns();
    bool is_idle = false;
    s32 cpu;

    cpu = scx_bpf_select_cpu_dfl(p, prev_cpu, wake_flags, &is_idle);
    if (is_idle) {
        stat_inc(0);	/* count local queueing */
        scx_bpf_dsq_insert(p, SCX_DSQ_LOCAL, MY_SLICE, 0);
        
        /* Debug print for CPU 4 */
        if (cpu == 4) {
            bpf_printk("CPU4: SELECT_CPU Task %d (pid=%d) queued to LOCAL DSQ, slice=%llu", 
                      p->pid, p->pid, MY_SLICE);
        }
    }
    u64 end = bpf_ktime_get_ns();

    max_runtime_add(0, end - start);

    return cpu;
}

void BPF_STRUCT_OPS(simple_enqueue, struct task_struct *p, u64 enq_flags)
{
    stat_inc(1);	/* count global queueing */

    u64 start = bpf_ktime_get_ns();

    if (fifo_sched) {
        scx_bpf_dsq_insert(p, SHARED_DSQ, MY_SLICE, enq_flags);
        bpf_printk("CPU4: ENQUEUE Task %d (pid=%d) enqueued to SHARED DSQ (FIFO mode), slice=%llu", 
                  p->pid, p->pid, MY_SLICE);
    } else {
        u64 vtime = p->scx.dsq_vtime;

        /*
        * Limit the amount of budget that an idling task can accumulate
        * to one slice.
        */
        if (time_before(vtime, vtime_now - MY_SLICE))
            vtime = vtime_now - MY_SLICE;

        scx_bpf_dsq_insert_vtime(p, SHARED_DSQ, MY_SLICE, vtime,
                    enq_flags);
        bpf_printk("CPU4: ENQUEUE Task %d (pid=%d) enqueued to SHARED DSQ (vtime mode), vtime=%llu, slice=%llu", 
                  p->pid, p->pid, vtime, MY_SLICE);
    }

    u64 end = bpf_ktime_get_ns();
    max_runtime_add(1, end - start);
}

void BPF_STRUCT_OPS(simple_dispatch, s32 cpu, struct task_struct *prev)
{
    u64 start = bpf_ktime_get_ns();

    if (cpu == 4) {
        bpf_printk("CPU4: DISPATCH prev_task=%d", prev ? prev->pid : -1);
        
        /* Check DSQ lengths before moving */
        u64 shared_len = scx_bpf_dsq_nr_queued(SHARED_DSQ);
        u64 local_len = scx_bpf_dsq_nr_queued(SCX_DSQ_LOCAL);
        bpf_printk("CPU4: Before dispatch - SHARED_DSQ len=%llu, LOCAL_DSQ len=%llu", 
                  shared_len, local_len);
    }

    /* If there's a previous task, re-enqueue it with updated vtime */
    if (prev && !fifo_sched) {
        /* Calculate what the previous task's vtime should be after one more slice */
        u64 weighted_vruntime_charge = (SCX_SLICE_DFL - prev->scx.slice) * 100 / prev->scx.weight;
        u64 forecasted_vtime = prev->scx.dsq_vtime + weighted_vruntime_charge;
        
        if (cpu == 4) {
            bpf_printk("CPU4: Re-enqueueing prev task %d, old_vtime=%llu, new_vtime=%llu", 
                      prev->pid, prev->scx.dsq_vtime, forecasted_vtime);
        }
        
        /* Update the task's vtime and re-enqueue it */
        prev->scx.dsq_vtime = forecasted_vtime;
        u64 shared_len_before = scx_bpf_dsq_nr_queued(SHARED_DSQ);
        scx_bpf_dsq_insert_vtime(prev, SHARED_DSQ, MY_SLICE, forecasted_vtime, 0);
        u64 shared_len_after = scx_bpf_dsq_nr_queued(SHARED_DSQ);
        if (cpu == 4) {
            bpf_printk("CPU4: shared len before=%llu, shared len after=%llu", 
                      shared_len_before, shared_len_after);
        }
        
        /* Update our tracking */
        u32 pid = prev->pid;
        u64 *existing_vruntime = bpf_map_lookup_elem(&process_vruntime, &pid);
        u64 new_weighted_vruntime;
        
        if (existing_vruntime) {
            new_weighted_vruntime = *existing_vruntime + weighted_vruntime_charge;
        } else {
            new_weighted_vruntime = forecasted_vtime;
        }
        
        update_process_vruntime(pid, new_weighted_vruntime);
    }

    /* Move a task from shared DSQ to local DSQ */
    scx_bpf_dsq_move_to_local(SHARED_DSQ);

    if (cpu == 4) {
        /* Check DSQ lengths after moving */
        u64 shared_len = scx_bpf_dsq_nr_queued(SHARED_DSQ);
        u64 local_len = scx_bpf_dsq_nr_queued(SCX_DSQ_LOCAL);
        bpf_printk("CPU4: After dispatch - SHARED_DSQ len=%llu, LOCAL_DSQ len=%llu", 
                  shared_len, local_len);
    }

    u64 end = bpf_ktime_get_ns();
    max_runtime_add(2, end - start);
}

void BPF_STRUCT_OPS(simple_running, struct task_struct *p)
{
    s32 cpu = bpf_get_smp_processor_id();
    
    if (cpu == 4) {
        bpf_printk("CPU4: RUNNING Task %d (pid=%d), vtime=%llu, slice=%llu", 
                  p->pid, p->pid, p->scx.dsq_vtime, p->scx.slice);
    }
    
    if (fifo_sched)
        return;
    /*
    * Global vtime always progresses forward as tasks start executing. The
    * test and update can be performed concurrently from multiple CPUs and
    * thus racy. Any error should be contained and temporary. Let's just
    * live with it.
    */
    if (time_before(vtime_now, p->scx.dsq_vtime))
        vtime_now = p->scx.dsq_vtime;
}

void BPF_STRUCT_OPS(simple_stopping, struct task_struct *p, bool runnable)
{
    s32 cpu = bpf_get_smp_processor_id();
    
    if (cpu == 4) {
        bpf_printk("CPU4: STOPPING Task %d (pid=%d), runnable=%d, remaining_slice=%llu", 
                  p->pid, p->pid, runnable, p->scx.slice);
    }
    
    if (fifo_sched)
        return;

    /*
    * Scale the execution time by the inverse of the weight and charge.
    *
    * Note that the default yield implementation yields by setting
    * @p->scx.slice to zero and the following would treat the yielding task
    * as if it has consumed all its slice. If this penalizes yielding tasks
    * too much, determine the execution time by taking explicit timestamps
    * instead of depending on @p->scx.slice.
    */
    u64 weighted_vruntime_charge = (SCX_SLICE_DFL - p->scx.slice) * 100 / p->scx.weight;
    p->scx.dsq_vtime += weighted_vruntime_charge;
    
    if (cpu == 4) {
        bpf_printk("CPU4: Task %d vruntime charge=%llu, new_vtime=%llu, weight=%d", 
                  p->pid, weighted_vruntime_charge, p->scx.dsq_vtime, p->scx.weight);
    }
    
    /* Track weighted vruntime per process for average calculation */
    u32 pid = p->pid;
    u64 *existing_vruntime = bpf_map_lookup_elem(&process_vruntime, &pid);
    u64 new_weighted_vruntime;
    
    if (existing_vruntime) {
        new_weighted_vruntime = *existing_vruntime + weighted_vruntime_charge;
    } else {
        /* New process, start with current dsq_vtime */
        new_weighted_vruntime = p->scx.dsq_vtime;
    }
    
    update_process_vruntime(pid, new_weighted_vruntime);
}

void BPF_STRUCT_OPS(simple_enable, struct task_struct *p)
{
    if (fifo_sched) {
        p->scx.dsq_vtime = vtime_now;
        return;
    }
    
    /* Use average vruntime for new processes to place them in the global DSQ */
    u64 avg_vruntime = get_average_vruntime();
    p->scx.dsq_vtime = avg_vruntime;
    
    /* Initialize the process in our tracking maps */
    u32 pid = p->pid;
    update_process_vruntime(pid, avg_vruntime);
}

void BPF_STRUCT_OPS(simple_disable, struct task_struct *p)
{
    if (fifo_sched)
        return;
    
    /* Remove process from tracking when it's disabled */
    u32 pid = p->pid;
    remove_process_vruntime(pid);
}

s32 BPF_STRUCT_OPS_SLEEPABLE(simple_init)
{
    return scx_bpf_create_dsq(SHARED_DSQ, -1);
}

void BPF_STRUCT_OPS(simple_exit, struct scx_exit_info *ei)
{
    UEI_RECORD(uei, ei);
}

SCX_OPS_DEFINE(simple_ops,
        .select_cpu		= (void *)simple_select_cpu,
        .enqueue			= (void *)simple_enqueue,
        .dispatch		= (void *)simple_dispatch,
        .running			= (void *)simple_running,
        .stopping		= (void *)simple_stopping,
        .enable			= (void *)simple_enable,
        .disable			= (void *)simple_disable,
        .init			= (void *)simple_init,
        .exit			= (void *)simple_exit,
        .name			= "simple");
