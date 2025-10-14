#!/bin/bash

# run with different amounts of cores, pipe the prints into out files



NUM_CORES=(2 4 8 16)

for NUM_CORE in ${NUM_CORES[@]}; do
    ./strict-weight/global-accounting-rlock $NUM_CORE 1000 10 > out/$NUM_CORE.txt
    sleep 0.5
done

