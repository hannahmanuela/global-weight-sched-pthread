#!/bin/bash


# check we're running as root
if [ $(id -u) -ne 0 ]; then
    echo "Please run as root"
    exit 1
fi


echo 1 > /sys/kernel/debug/tracing/tracing_on
./test_policy/test &
pid=$!

sleep 4

cd bpf-stuff
./scx_simple &
bpf_pid=$!

cd ..

sleep 2

kill -9 $bpf_pid

# Kill the entire process tree
pkill -TERM -P $pid
kill -TERM $pid
sleep 1
pkill -KILL -P $pid

echo 0 > /sys/kernel/debug/tracing/tracing_on
