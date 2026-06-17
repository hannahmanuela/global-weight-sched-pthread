#!/bin/bash

# NUM_CORES=(1 2 4 8 16 24 32 40 48)
NUM_CORES=(1 2 4)
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
