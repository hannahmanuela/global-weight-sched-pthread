Global-heap is a design to implement cgroups with global weights.  The
cgroup documentation specifies that each group should get CPU time
proportional to its weight as a share of the sum of weights of
runnable groups.

In global-heap each group has a weight and a list of runnable
processes in the group.  Global-heap maintains a `spec_virt_time` for
each group and selects a process with the lowest `spec_virt_time` to
run on a core.

When a simulation starts each group's spec_virt_time is 0.

After proc has run, global-heap updates spec_virt_time to
tick_length/weight. Consider a ticket_length of 1000us and a group
with weight of 1. If there are enough groups to fill a tick_length,
then the group with weight 1 is selected every 1000 virt time. If
weight is 10, then the group gets to run every 100 virt time.  If
there are two groups with weight 10, each of them gets to run every
100 virt time.

If a process doesn't run for a full tick length, its virt time is
moved down, so it runs earlier, based on how much it has left.

When a group is added (runnable threads goes from 0 to 1), it gets the
average spec_virt_time of runnable groups, adjusted for lag.  Lag is
the time between the group spec_virt_time and the average virt
time. If lag is negative, then spec_virt_time is after the average
virt time; otherwise, it is before.

XXX examples.





