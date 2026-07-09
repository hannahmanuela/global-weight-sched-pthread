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
SCHEDULERS=("rr" "gwfs" "gq" "rr1")
# SCHEDULERS=("rr" "gwfs" "gq" "rr1" "gppcrq")
D=exp-out-`date +%Y-%m-%d_%H-%M-%S`

mkdir $D
echo $D

# Run "$@" once per core count in NUM_CORES, appending "<n> <n*4>", and
# collect the "tp" lines from the combined output into $D/tp-<label>.dat.
sweep2() {
    local msg="$1" label="$2"
    shift 2
    echo "$msg"
    sleep 1
    for n in ${NUM_CORES[@]}; do
        "$@" $n $(($n * 4))
    done 2>&1 > $D/tp-$label.out
    grep tp $D/tp-$label.out | awk '{print $2, $3}' > $D/tp-$label.dat
}

# Like sweep2, but appends only "<n>" (no core-count multiplier).
sweep1() {
    local msg="$1" label="$2"
    shift 2
    echo "$msg"
    sleep 1
    for n in ${NUM_CORES[@]}; do
        "$@" $n
    done 2>&1 > $D/tp-$label.out
    grep tp $D/tp-$label.out | awk '{print $2, $3}' > $D/tp-$label.dat
}

# Run "$@" once and collect the "bin" lines into $D/rankprio-<label>.dat.
rankprio() {
    local msg="$1" label="$2"
    shift 2
    echo "$msg"
    sleep 1
    "$@" 2>&1 > $D/rankprio-$label.out
    grep "bin" $D/rankprio-$label.out | awk '{print $2, $3}' > $D/rankprio-$label.dat
}

for s in ${SCHEDULERS[@]}; do
    sweep2 "$s" "$s" ./schedule -y $s
done

sweep1 "mheap" "mheap" ./test-mheap

sweep2 "rr w b=2" "rr-prio" ./schedule -r 2 -b 2 -g 2 rr

sweep2 "rr w b=2 and preempt" "rr-prio-mask" ./schedule -p -r 2 -b 2 -g 2 rr

sweep2 "rr1 w b=2 and preempt" "rr1-prio-mask" ./schedule -p -r 2 -b 2 -g 2 rr1

sweep2 "gppcrq w b=2 and preempt" "gppcrq-prio" ./schedule -p -r 2 -b 2 -g 2 gppcrq

rankprio "rr1 rank errror" "rr1" ./rankprioerror.sh rr1 4 log

rankprio "rr rank errror" "rr" ./rankprioerror.sh rr 4 log
