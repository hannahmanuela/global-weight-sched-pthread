#!/bin/bash


SECS_TO_RUN=3
WEIGHT_RATIO=2

NUM_CORES=(1 2 4 6 8 10 12 14 16)


for NUM_CORE in ${NUM_CORES[@]}; do
    echo "running with $NUM_CORE"
    ./global-heap -t $SECS_TO_RUN -r $WEIGHT_RATIO $NUM_CORE $(($NUM_CORE * 5)) > out/$NUM_CORE.txt
    echo "done with $NUM_CORE"
done