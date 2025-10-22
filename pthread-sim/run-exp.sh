#!/bin/bash

# run with different amounts of cores, pipe the prints into out files

TICK_LENGTH_US=500
NUM_GROUPS=1000

NUM_CORES=(2 4 8 16 27)

for NUM_CORE in ${NUM_CORES[@]}; do
    echo "running with $NUM_CORE"
    ./strict-weight/global-accounting-rlock $NUM_CORE $TICK_LENGTH_US $NUM_GROUPS > out/$NUM_CORE.txt
    echo "done with $NUM_CORE"
done

