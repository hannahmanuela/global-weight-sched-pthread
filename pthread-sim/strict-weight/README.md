Global-heap is a design to implement cgroups with global weights.  The
cgroup documentation specifies that each group should get CPU time
proportional to its weight as a share of the sum of weights of
runnable groups.

In global-heap each group has a weight and a list of runnable
processes in the group.  Global-heap maintains a `vruntime` for
each group and selects a process with the lowest `vruntime` to
run on a core.

When a simulation starts each group's vruntime is 0. The accounting is explained below, 
and mirrors the way linux's old CFS scheduler accounted time. (an aside: we do not use the newer 
EEVDF accounting because we do not need to maintain eligibility of processes).

After proc has run, global-heap updates its group's vruntime to tick_length/weight. 

Consider a tick_length of 1000us and two groups, g1 with weight 10 and g2 with weight 20. 
The first core to schedule arbitrarily picks g1, and immediately updates g1's vruntime to be 1000/10 = 100.
Updating the time immediately upon picking the group ensures that, if another core picks, 
it won't simply pick the same group. When the same core later or another core immediately picks 
again, g2 will have the lower vruntime and the core will run a process from it; but g2's weight 
is 20 so its vruntime will only be updated to be 1000/20 = 50. We thus ensures a 2:1 ratio of core runtime.

If a process doesn't run for a full tick length, its vruntime is
moved down by the difference. For instance, if the initial process from g1 in 
the example above exist after 500us, then the group's vruntime would be updated by
the diff to the expected (-500) divided by the weight (10) = -50; leaving the group with a 
vruntime as if the core had only added the time it actually ran.

When a group's last process exits, the group "goes to sleep". In that case, the 
difference between its vruntime and the systems minimum vruntime is stored. This 
represents its "lag", ie what was its position in the heap before it exited.

When the group wakes up, its vruntime is set to be the current minimum of runnable groups, 
adjusted for lag. We do not currently degrade the lag over time; although we could.



To implement the global heap in scalable way, it uses multiple
heaps, inspired by https://dl.acm.org/doi/10.1145/2755573.2755616.





