#ifndef _GW_SCHED_H_

#define _GW_SCHED_H_

#include <stdint.h>

#include "process.h"

struct rq;

typedef uint64_t u64;

struct gw_scheduler {
        const char *name;

        /* Called once at boot from init_gw_global(). */
        void (*init)(void);

        /*
         * Group/lag bookkeeping for p transitioning runnable<->not-runnable.
         * MUST fire exactly once per transition, regardless of which path the
         * class glue ultimately takes for p (heap insert, mailbox handoff, or
         * "already running on this rq" no-op). For WFS this is where nthreads
         * is bumped and the group's vruntime is anchored on 0->1 / snapshotted
         * on 1->0. Caller holds rq->lock.
         */
        void (*account_wakeup)(struct task_struct *p);
        void (*account_sleep)(struct task_struct *p);

        /*
         * Heap/runqueue membership. Decoupled from account_wakeup/sleep so
         * that paths which keep p off the heap (currently-running task,
         * mailbox handoff) still pair their group bookkeeping. Both ops
         * must tolerate redundant calls: put is a no-op if p->gw.on_list,
         * take is a no-op if !p->gw.on_list. Caller holds rq->lock.
         */
        void (*put_task_in_rq)(struct task_struct *p);
        void (*take_task_from_rq)(struct task_struct *p);

        /*
         * Issue a pre-charge for p without placing it on the heap. Used by
         * the bypass paths in enqueue_task_gw (task_current, local-idle
         * mailbox, remote-idle mailbox kick), where p will run without
         * transiting put_task_in_rq -- the matching account_wfs at
         * put_prev/dequeue still needs a pre-charge to refund against.
         * Optional: back-ends without vt accounting (RR) leave this NULL.
         * Caller holds rq->lock.
         */
        void (*charge_vt)(struct task_struct *p);

        /* Apply delta_exec ns of just-run CPU time to p's group/lag
         * accounting. Called from update_curr_gw() whenever the common
         * accounting path advances exec_start, so every nanosecond
         * credited to sum_exec_runtime is also seen here exactly once,
         * regardless of which path (pick, put_prev, task_tick, ...)
         * drove the update. RR has no per-group accounting and uses a
         * no-op. Caller holds rq->lock. */
        void (*account)(struct task_struct *p, u64 delta_exec);

        /* Pick next task on this CPU. May return prev itself if no
         * better candidate exists. NULL means no GW work for this CPU.
         * Accounting for prev's just-run time is already done via
         * ->account; the back-end only needs to read the now-current
         * vt/lag state and decide. Caller holds rq->lock. */
        struct task_struct *(*schedule)(struct rq *rq, struct task_struct *prev);

        /* prev yielded its quantum (task_tick path). Accounting already
         * done by ->account; this is for any extra policy bookkeeping. */
        void (*yield)(struct task_struct *p);

        /*
         * On enqueue, try to find a remote CPU we can hand p to via the
         * shared mailbox + IPI path. Returns the target CPU (with its
         * gw_busy_mask bit already claimed via gw_remote_mark_busy) or
         * -1 if no suitable CPU exists.
         *
         * The shared mailbox write and resched_cpu() are done by the
         * caller (enqueue_task_gw), so back-ends only own the policy of
         * which CPU to target. WFS just hunts for idle CPUs; RR can
         * additionally consider CPUs that are running best-effort tasks.
         */
        int (*pick_idle_target)(struct task_struct *p);

        /* True iff any GW task is currently queued in this back-end's
         * structures (heap, mailboxes, ...). Used by the mode-switch
         * syscall to ensure a clean handoff. */
        bool (*any_queued)(void);

        /* Forwarded gw_set_nheaps syscall. */
        int (*set_nheaps)(int n);
};

#endif
