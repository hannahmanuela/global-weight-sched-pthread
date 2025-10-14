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

UEI_DEFINE(uei);


// =======================================================
// DEFINES
// =======================================================


/*
* Built-in DSQs such as SCX_DSQ_GLOBAL cannot be used as priority queues
* (meaning, cannot be dispatched to with scx_bpf_dsq_insert_vtime()). We
* therefore create a separate DSQ with ID 0 that we dispatch to and consume
* from.
*/
#define SHARED_DSQ 0

#define MY_SLICE ((__u64)4 * 1000000) // 4ms


struct process_info {
    u64 vruntime; // this is weighted, ie its unit is time per unit weight
    u64 vlag;
    u64 last_vruntime; // the vruntime at the time it blocked
    u64 runnable;
};

/* Map of processes */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(key_size, sizeof(u32));	/* pid */
    __uint(value_size, sizeof(struct process_info));
    __uint(max_entries, 10000);
} process_info SEC(".maps");


u64 curr_vtime = 0;
u64 curr_min_vtime = 0;
u64 curr_total_weight = 0;


// =======================================================
// HELPER FUNCTIONS
// =======================================================


static __always_inline __u64 safe_div_u64(__u64 a, __u64 b)
{
    if (b == 0)
        return (u64)-1;
    return a / b;
}

// Helper function to update process vruntime and maintain average
// 
static void update_process_vruntime(u32 pid, u64 weighted_vruntime_gotten)
{
    struct process_info *existing_process_info = bpf_map_lookup_elem(&process_info, &pid);
    u64 new_process_vruntime = 0;
    u64 old_process_vruntime = 0;

    // update process info map
    if (!existing_process_info) {
        bpf_printk("ERROR in update func: no process info found for task %d??", pid);
        return;
    }
    old_process_vruntime = existing_process_info->vruntime;
    new_process_vruntime = old_process_vruntime + weighted_vruntime_gotten;
    {
        struct process_info updated = *existing_process_info;
        updated.vruntime = new_process_vruntime;
        bpf_map_update_elem(&process_info, &pid, &updated, BPF_ANY);
    }
    
}

// Helper function to get average vruntime
// static u64 get_average_vruntime(void)
// {
//     u32 total_idx = 0, count_idx = 1;
//     u64 *total_vruntime, *process_count;
    
//     total_vruntime = bpf_map_lookup_elem(&overall_time, &total_idx);
//     process_count = bpf_map_lookup_elem(&overall_time, &count_idx);
    
//     if (!total_vruntime || !process_count || *process_count == 0)
//         return 0; // Fallback to 0? what if they are all currently running?
    
//     return safe_div_u64(*total_vruntime, *process_count);
// }

// Helper function to remove process from tracking
static void remove_process_vruntime(u32 pid)
{
    struct process_info *existing_process_info = bpf_map_lookup_elem(&process_info, &pid);
    u32 total_idx = 0, count_idx = 1;
    u64 *total_vruntime, *process_count;
    
    if (!existing_process_info)
        return;
    
    // Remove from process map
    bpf_map_delete_elem(&process_info, &pid);
}

static long find_min_callback(void *map, const void *key, void *value, void *data)
{
    struct process_info *pi = value;
    u64 *min_val = data;
    if (pi && pi->runnable && pi->vruntime < *min_val)
        *min_val = pi->vruntime;
    return 0; // continue iteration
}

static void update_min_vruntime()
{
    // compute into a stack variable and then store to map value
    u64 local_min = ~0ULL; // U64_MAX
    bpf_for_each_map_elem(&process_info, find_min_callback, &local_min, 0);
    curr_min_vtime = local_min;
    
}



// =======================================================
// BPF OPS
// =======================================================
// NOTE: comments above functions are copied from ext.c

/*
* Either we're loading a BPF scheduler or a new task is being forked.
* Initialize @p for BPF scheduling. This operation may block and can
* be used for allocations, and is called exactly once for a task.
*
* Return 0 for success, -errno for failure. An error return while
* loading will abort loading of the BPF scheduler. During a fork, it
* will abort that specific fork.
*/
s32 BPF_STRUCT_OPS(simple_init_task, struct task_struct *p)
{
    s32 cpu = bpf_get_smp_processor_id();


    if (cpu == 4) {
        bpf_printk("CPU4: INIT_TASK Task %d, vtime=%llu", p->pid, curr_vtime);
    }

    struct process_info existing_process_info = {
        .vruntime = curr_vtime,
        .vlag = 0,
        .runnable = 0,
        .last_vruntime = curr_vtime
    };
    {
        u32 pid = p->pid;
        bpf_map_update_elem(&process_info, &pid, &existing_process_info, BPF_ANY);
    }

    curr_total_weight += p->scx.weight;

    return 0;
}

/*
* Enable @p for BPF scheduling. enable() is called on @p any time it
* enters SCX, and is always paired with a matching disable().
*/
void BPF_STRUCT_OPS(simple_enable, struct task_struct *p)
{
    // copied over from init_task
    s32 cpu = bpf_get_smp_processor_id();

    if (cpu == 4) {
            bpf_printk("CPU4: ENABLE Task %d", p->pid);
    }

    struct process_info existing_process_info = {
        .vruntime = curr_vtime,
        .vlag = 0,
        .runnable = 0,
        .last_vruntime = curr_vtime
    };
    {
        u32 pid = p->pid;
        bpf_map_update_elem(&process_info, &pid, &existing_process_info, BPF_ANY);
    }


    curr_total_weight += p->scx.weight;

}

/*
* @p is exiting or the BPF scheduler is being unloaded. Perform any
* necessary cleanup for @p.
*/
s32 BPF_STRUCT_OPS(simple_exit_task, struct task_struct *p)
{
    s32 cpu = bpf_get_smp_processor_id();
    
    if (cpu == 4) {
        bpf_printk("CPU4: EXIT_TASK Task %d", p->pid);
    }

    curr_total_weight -= p->scx.weight;

    u32 pid = p->pid;
    struct process_info *existing_process_info = bpf_map_lookup_elem(&process_info, &pid);
    if (!existing_process_info) {
        bpf_printk("ERROR in exit_task func: no process info found for task %d??", pid);
        return -1;
    }
    curr_vtime -= safe_div_u64(existing_process_info->vlag, curr_total_weight);

    remove_process_vruntime(pid);

    update_min_vruntime();

    return 0;

}

/*
* @p is exiting, leaving SCX or the BPF scheduler is being unloaded.
* Disable BPF scheduling for @p. A disable() call is always matched
* with a prior enable() call.
*/
void BPF_STRUCT_OPS(simple_disable, struct task_struct *p)
{
    s32 cpu = bpf_get_smp_processor_id();
    
    if (cpu == 4) {
        bpf_printk("CPU4: DISABLE Task %d", p->pid);
    }

    curr_total_weight -= p->scx.weight;

    u32 pid = p->pid;
    struct process_info *existing_process_info = bpf_map_lookup_elem(&process_info, &pid);
    if (!existing_process_info) {
        bpf_printk("ERROR in disable func: no process info found for task %d??", pid);
        return;
    }
    curr_vtime -= safe_div_u64(existing_process_info->vlag, curr_total_weight);

    remove_process_vruntime(pid);

    update_min_vruntime();

}


/*
* Decision made here isn't final. @p may be moved to any CPU while it
* is getting dispatched for execution later. However, as @p is not on
* the rq at this point, getting the eventual execution CPU right here
* saves a small bit of overhead down the line.
*
* If an idle CPU is returned, the CPU is kicked and will try to
* dispatch. While an explicit custom mechanism can be added,
* select_cpu() serves as the default way to wake up idle CPUs.
*
* @p may be inserted into a DSQ directly by calling
* scx_bpf_dsq_insert(). If so, the ops.enqueue() will be skipped.
* Directly inserting into %SCX_DSQ_LOCAL will put @p in the local DSQ
* of the CPU returned by this operation.
*
* Note that select_cpu() is never called for tasks that can only run
* on a single CPU or tasks with migration disabled, as they don't have
* the option to select a different CPU. See select_task_rq() for
* details.
*/
s32 BPF_STRUCT_OPS(simple_select_cpu, struct task_struct *p, s32 prev_cpu, u64 wake_flags)
{
    bool is_idle = false;
    s32 cpu;

    cpu = scx_bpf_select_cpu_dfl(p, prev_cpu, wake_flags, &is_idle);
    if (is_idle) {
        scx_bpf_dsq_insert(p, SCX_DSQ_LOCAL, MY_SLICE, 0);
    }

    if (cpu == 4) {
        bpf_printk("CPU4: SELECT_CPU Task %d (pid=%d) queued to LOCAL DSQ, slice=%llu", 
                  p->pid, p->pid, MY_SLICE);
    }

    return cpu;
}

/*  
* @p is ready to run. Insert directly into a DSQ by calling
* scx_bpf_dsq_insert() or enqueue on the BPF scheduler. If not directly
* inserted, the bpf scheduler owns @p and if it fails to dispatch @p,
* the task will stall.
*
* If @p was inserted into a DSQ from ops.select_cpu(), this callback is
* skipped.
*/
void BPF_STRUCT_OPS(simple_enqueue, struct task_struct *p, u64 enq_flags)
{

    s32 cpu = bpf_get_smp_processor_id();

    scx_bpf_dsq_insert_vtime(p, SHARED_DSQ, MY_SLICE, p->scx.dsq_vtime, enq_flags);
    if (cpu == 4) {
        bpf_printk("CPU4: ENQUEUE Task %d enqueued to SHARED DSQ, curr vtime=%llu, p vtime=%llu, slice=%llu", 
                    p->pid, curr_vtime, p->scx.dsq_vtime, MY_SLICE);
    }

}

/*
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
void BPF_STRUCT_OPS(simple_dispatch, s32 cpu, struct task_struct *prev)
{

    if (cpu == 4) {
        u64 shared_len = scx_bpf_dsq_nr_queued(SHARED_DSQ);
        u64 local_len = scx_bpf_dsq_nr_queued(SCX_DSQ_LOCAL);
        bpf_printk("CPU4: DISPATCH curr_vtime=%llu, prev_task=%d, SHARED_DSQ len=%llu, LOCAL_DSQ len=%llu", curr_vtime, prev ? prev->pid : -1, shared_len, local_len);
    }

    // if the previous task is still runnable, need to check if it's owed time (ie shld continue) or not
    if (prev && prev->scx.flags & SCX_TASK_QUEUED) {

        bpf_printk("  prev_vtime=%llu, min_vtime=%llu", prev->scx.dsq_vtime, curr_min_vtime);
        if (prev->scx.dsq_vtime <= curr_min_vtime) {
            // should keep running the previous task
            u64 virt_time_gotten = safe_div_u64(MY_SLICE - prev->scx.slice, prev->scx.weight); 
            update_process_vruntime(prev->pid, virt_time_gotten);
            prev->scx.dsq_vtime += virt_time_gotten;
            prev->scx.slice = MY_SLICE;

            curr_vtime += safe_div_u64(virt_time_gotten, curr_total_weight);
            update_min_vruntime();
            return;
        }

    }

    /* Move a task from shared DSQ to local DSQ */
    scx_bpf_dsq_move_to_local(SHARED_DSQ);

    update_min_vruntime();

}

/*
* This operation is called every 1/HZ seconds on CPUs which are
* executing an SCX task. Setting @p->scx.slice to 0 will trigger an
* immediate dispatch cycle on the CPU.
*/
// void BPF_STRUCT_OPS(simple_tick, struct task_struct *p)
// {
//     // I set the slice to be small, so for now do nothing
// }

/*
* This and the following three functions can be used to track a task's
* execution state transitions. A task becomes ->runnable() on a CPU,
* and then goes through one or more ->running() and ->stopping() pairs
* as it runs on the CPU, and eventually becomes ->quiescent() when it's
* done running on the CPU.
*
* @p is becoming runnable on the CPU because it's
*
* - waking up (%SCX_ENQ_WAKEUP)
* - being moved from another CPU
* - being restored after temporarily taken off the queue for an
*   attribute change.
*
* This and ->enqueue() are related but not coupled. This operation
* notifies @p's state transition and may not be followed by ->enqueue()
* e.g. when @p is being dispatched to a remote CPU, or when @p is
* being enqueued on a CPU experiencing a hotplug event. Likewise, a
* task may be ->enqueue()'d without being preceded by this operation
* e.g. after exhausting its slice.
*/
void BPF_STRUCT_OPS(simple_runnable, struct task_struct *p)
{
    s32 cpu = bpf_get_smp_processor_id();

    curr_total_weight += p->scx.weight;

    u32 pid = p->pid;
    struct process_info *existing_process_info = bpf_map_lookup_elem(&process_info, &pid);
    if (!existing_process_info) {
        bpf_printk("ERROR in runnable func: no process info found for task %d??", pid);
        return;
    }

    u64 vlag = existing_process_info->vlag;
    // TODO: implement delay deq - only add what's left of the lag? how do we know what's left?
    // probably there's some math with the last_virt_time we could use

    u64 old_vtime = curr_vtime;
    curr_vtime -= safe_div_u64(vlag, curr_total_weight);

    if (cpu == 4) {
        bpf_printk("CPU4: RUNNABLE Task %d, old vtime=%llu, new vtime=%llu", 
                  pid, old_vtime, curr_vtime);
    }

    {
        struct process_info updated = *existing_process_info;
        updated.runnable = 1;
        updated.vruntime = curr_vtime;
        bpf_map_update_elem(&process_info, &pid, &updated, BPF_ANY);
    }


    // update min_vruntime if necessary
    if (curr_vtime < curr_min_vtime) {
        curr_min_vtime = curr_vtime;
    }
}

/*
* See ->runnable() for explanation on the task state notifiers.
*/
void BPF_STRUCT_OPS(simple_running, struct task_struct *p)
{
    s32 cpu = bpf_get_smp_processor_id();
    
    if (cpu == 4) {
        bpf_printk("CPU4: RUNNING Task %d (pid=%d), vtime=%llu, slice=%llu", 
                  p->pid, p->pid, p->scx.dsq_vtime, p->scx.slice);
    }

    // update min_vruntime (basically, deqing to run and will need to know what the min is of the leftover processes)
    update_min_vruntime();

}

/*
* See ->runnable() for explanation on the task state notifiers. If
* !@runnable, ->quiescent() will be invoked after this operation
* returns.
*/
void BPF_STRUCT_OPS(simple_stopping, struct task_struct *p, bool runnable)
{
    s32 cpu = bpf_get_smp_processor_id();
    
    if (cpu == 4) {
        bpf_printk("CPU4: STOPPING Task %d (pid=%d), runnable=%d, remaining_slice=%llu", 
                  p->pid, p->pid, runnable, p->scx.slice);
    }

    // apparently scx.slice holds the time left over? And it should have run for its full slice
    // also: 
    //  * Note that the default yield implementation yields by setting
	//  * @p->scx.slice to zero and the following would treat the yielding task
	//  * as if it has consumed all its slice. If this penalizes yielding tasks
	//  * too much, determine the execution time by taking explicit timestamps
	//  * instead of depending on @p->scx.slice.
    u64 virt_time_gotten = safe_div_u64(MY_SLICE - p->scx.slice, p->scx.weight); 
    update_process_vruntime(p->pid, virt_time_gotten);
    p->scx.dsq_vtime += virt_time_gotten;

    curr_vtime += safe_div_u64(MY_SLICE - p->scx.slice, curr_total_weight);

}

/*
* See ->runnable() for explanation on the task state notifiers.
*
* @p is becoming quiescent on the CPU because it's
*
* - sleeping (%SCX_DEQ_SLEEP)
* - being moved to another CPU
* - being temporarily taken off the queue for an attribute change
*   (%SCX_DEQ_SAVE)
*
* This and ->dequeue() are related but not coupled. This operation
* notifies @p's state transition and may not be preceded by ->dequeue()
* e.g. when @p is being dispatched to a remote CPU.
*/
void BPF_STRUCT_OPS(simple_quiescent, struct task_struct *p)
{
    s32 cpu = bpf_get_smp_processor_id();
    
    if (cpu == 4) {
        bpf_printk("CPU4: QUIESCENT Task %d (pid=%d), vtime=%llu, slice=%llu", 
                  p->pid, p->pid, p->scx.dsq_vtime, p->scx.slice);
    }

    u64 diff = p->scx.dsq_vtime - curr_vtime;
    u32 pid = p->pid;
    struct process_info *existing_process_info = bpf_map_lookup_elem(&process_info, &pid);
    if (!existing_process_info) {
        bpf_printk("ERROR no process info found for task %d??", p->pid);
        return;
    }
    {
        struct process_info updated = *existing_process_info;
        updated.vlag = diff;
        updated.runnable = 0;
        updated.last_vruntime = p->scx.dsq_vtime;
        bpf_map_update_elem(&process_info, &pid, &updated, BPF_ANY);
    }

    curr_total_weight -= p->scx.weight;

    curr_vtime += safe_div_u64(diff, curr_total_weight);

    update_min_vruntime();

}


s32 BPF_STRUCT_OPS_SLEEPABLE(simple_init)
{
    return scx_bpf_create_dsq(SHARED_DSQ, -1);
}

void BPF_STRUCT_OPS(simple_exit, struct scx_exit_info *ei)
{
    UEI_RECORD(uei, ei);
}


/*

The following pseudo-code summarizes the entire lifecycle of a task managed by a sched_ext scheduler:
(source: https://docs.kernel.org/scheduler/sched-ext.html)


ops.init_task();            // A new task is created 
ops.enable();               // Enable BPF scheduling for the task 

while (task in SCHED_EXT) {
    if (task can migrate)
        ops.select_cpu();   // Called on wakeup (optimization)

    ops.runnable();         // Task becomes ready to run

    while (task is runnable) {
        if (task is not in a DSQ && task->scx.slice == 0) {
            ops.enqueue();  // Task can be added to a DSQ

            // Any usable CPU becomes available

            ops.dispatch(); // Task is moved to a local DSQ
        }
        ops.running();      // Task starts running on its assigned CPU
        while (task->scx.slice > 0 && task is runnable)
            ops.tick();     // Called every 1/HZ seconds
        ops.stopping();     // Task stops running (time slice expires or wait)

        // Task's CPU becomes available

        ops.dispatch();     // task->scx.slice can be refilled
    }

    ops.quiescent();        // Task releases its assigned CPU (wait)
}

ops.disable();              // Disable BPF scheduling for the task
ops.exit_task();            // Task is destroyed

*/
SCX_OPS_DEFINE(simple_ops,
        .init_task		= (void *)simple_init_task,
        .select_cpu		= (void *)simple_select_cpu,
        .runnable		= (void *)simple_runnable,
        .enqueue		= (void *)simple_enqueue,
        .dispatch		= (void *)simple_dispatch,
        // .tick			= (void *)simple_tick,
        .running		= (void *)simple_running,
        .stopping		= (void *)simple_stopping,
        .quiescent		= (void *)simple_quiescent,
        .exit_task		= (void *)simple_exit_task,

        .enable			= (void *)simple_enable,
        .disable			= (void *)simple_disable,
        .init			= (void *)simple_init,
        .exit			= (void *)simple_exit,

        // .core_sched_before	= (void *)simple_core_sched_before, // this could be interesting if we push multiple ps to a core
        // .set_cpumask		= (void *)simple_set_cpumask, // do we need to implement this or will it just do the right thing?

        // .cgroup_init 	= (void *)simple_cgroup_init,
        // .group_exit		= (void *)simple_group_exit,
        // .cgroup_prep_move	= (void *)simple_cgroup_prep_move,
        // .group_move		= (void *)simple_group_move,
        // .cgroup_set_weight = (void *)simple_cgroup_set_weight,

        .name			= "simple");
