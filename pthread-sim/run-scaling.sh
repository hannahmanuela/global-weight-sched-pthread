#!/bin/bash


SECS_TO_RUN=3

NUM_CORES=(1 2 4 6 8 10 12 14 16 20 24 28 32 36 40 44 48 52 56)

# Policies from driver.c: 0=seq, 1=numa-first, 2=phys-first
declare -A POLICY_NAMES=( [0]="seq" [1]="numa-first" [2]="phys-first" )

for POLICY in 0 1 2; do
    POLICY_NAME=${POLICY_NAMES[$POLICY]}
    echo "=== policy $POLICY ($POLICY_NAME) ==="

    for NUM_CORE in ${NUM_CORES[@]}; do
        echo "running $POLICY_NAME with $NUM_CORE"

        OUT_DIR="out/$POLICY_NAME/$NUM_CORE"
        rm -rf $OUT_DIR
        mkdir -p $OUT_DIR

        COMMAND="./global-heap -s -t $SECS_TO_RUN -P $POLICY $NUM_CORE $(($NUM_CORE * 5)) > $OUT_DIR/vals.txt"

        sudo /home/hannahmanuela/perf-tools/bin/perf record -o $OUT_DIR/perf.data -F 500 -g -- sh -c "$COMMAND"

        sudo /home/hannahmanuela/perf-tools/bin/perf report -n --stdio -i $OUT_DIR/perf.data > $OUT_DIR/call_graph.txt

        echo "done with $POLICY_NAME $NUM_CORE"
    done
done
