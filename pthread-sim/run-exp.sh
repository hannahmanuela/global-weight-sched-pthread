#!/bin/bash

# run with different amounts of cores, pipe the prints into out files



NUM_CORES=(2 4 8 16 27)

for NUM_CORE in ${NUM_CORES[@]}; do
    echo "running with $NUM_CORE"
    ./strict-weight/global-accounting-rlock $NUM_CORE 4000 10 > out/$NUM_CORE.txt
    echo "done with $NUM_CORE"
done

