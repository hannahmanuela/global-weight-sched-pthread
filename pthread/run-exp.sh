#!/bin/bash

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <ncore>"
    exit 1
fi

NUM_CORES=(1)
n=2
while [ $n -le $1 ]; do
    NUM_CORES+=($n)
    if [ $n -lt 8 ]; then
        n=$((n + 2))
    else
        n=$((n + 8))
    fi
done
SCHEDULERS=("rr" "gwfs" "gq" "pcrq")
D=exp-out-`date +%Y-%m-%d_%H-%M-%S`

mkdir $D
echo $D

for s in ${SCHEDULERS[@]}; do
    echo $s
    for n in ${NUM_CORES[@]}; do
	./schedule -y $s $n $(($n * 4))
    done 2>&1 > $D/tp-$s.out
    grep tp $D/tp-$s.out | awk '{print $2, $3}' > $D/tp-$s.dat
done

echo "mheap"

for n in ${NUM_CORES[@]}; do
	./test-mheap $n
done 2>&1 > $D/tp-mheap.out
grep tp $D/tp-mheap.out | awk '{print $2, $3}' > $D/tp-mheap.dat

echo "rr w priority and preemption and runq"

for n in ${NUM_CORES[@]}; do
    ./schedule -p -q -r 2 -b 2 -g 2 rr $n $(($n * 4))
done 2>&1 > $D/tp-rr-prio.out
grep tp $D/tp-rr-prio.out | awk '{print $2, $3}' > $D/tp-rr-prio.dat
