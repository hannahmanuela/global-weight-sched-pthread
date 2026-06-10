#!/bin/sh

# interesting scenarios for rr

echo "==== scale under high load"
./schedule rr 4 64

echo "==== scalability under low load"
./schedule rr 4 4

echo "==== sleeping process 0 will run after all other runnable processes"
./schedule -b 1 rr 4 64

echo "==== two priority levels, hitting the preempt path"
./schedule -p -b 2 -g 2 rr 4 6

echo "==== two priority levels, hitting the running queue"
./schedule -p -q -b 2 -g 2 rr 4 6
