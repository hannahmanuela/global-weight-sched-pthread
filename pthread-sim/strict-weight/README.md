Global-heap is a scheduler that schedule processes in accordance to
global weight of the cgroup that a process belongs to.  The cgroup
documentation specifies that each group should get CPU time
proportional to its weight as a share of the sum of weights of
runnable groups.

In global-heap each group has a weight and a list of runnable
processes in the group.  Global-heap maintains a `vruntime` for each
group and selects a process with the lowest `vruntime` to run on a
core.

Consider a tick_length of 1000us and two groups, g1 with weight 10 and
g2 with weight 20.  The first core to schedule arbitrarily picks g1,
and immediately updates g1's vruntime to be 1000/10 = 100.  Updating
the time immediately upon picking the group ensures that, if another
core picks, it won't simply pick the same group. When the same core
later or another core immediately picks again, g2 will have the lower
vruntime and the core will run a process from it; but g2's weight is
20 so its vruntime will only be updated to be 1000/20 = 50. We thus
ensures a 2:1 ratio of core runtime.

Updating the g1's runtime immediately also ensures that when another
process becomes runnable it will get a higher vruntime than the
process just selected. 

If a process doesn't run for a full tick length, the group's vruntime
is moved down by the difference. For instance, if the initial process
from g1 in the example above exist after 500us, then the group's
vruntime would be updated by the diff to the expected (-500) divided
by the weight (10) = -50; leaving the group with a vruntime as if the
core had only added the time it actually ran. 

Other processes of the same group will not have their vruntimes not
moved back because the process didn't run for its full vruntime.  If
the same process runs again immediately, then it will benefit from the
updated group vruntime.   This also avoids the need to update the
vruntimes of enqueued processes of the same group, and having to
update the global heap of runnable processes.

When a group's last process exits, the group "goes to sleep". In that
case, the system min vruntime is stored in the group's min_vt_deq.
When a process of the group becomes runnable and is enqueud, the
difference between this group's vruntime and min_vt_deq represents its
"lag", ie what was its position in the heap before it exited.  The
lag is adjusted with systems min vruntime, which may have decreased
since the group was dequeued.

To implement the global heap in scalable way, it uses multiple
heaps, inspired by https://dl.acm.org/doi/10.1145/2755573.2755616.





