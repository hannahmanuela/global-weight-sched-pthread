#!/bin/bash


if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <scheduler (rr, gwfs)>"
    exit 1
fi



SCHEDULER=$1

SECS_TO_RUN=3

NUM_CORES=(1 2 4 6 8 10 12 14 16 20 24 28)

for NUM_CORE in ${NUM_CORES[@]}; do
    echo "running $POLICY with $NUM_CORE"

    OUT_DIR="out/$POLICY/$NUM_CORE"
    rm -rf $OUT_DIR
    mkdir -p $OUT_DIR

    COMMAND="./schedule $SCHEDULER -t $SECS_TO_RUN $NUM_CORE $(($NUM_CORE * 3)) > $OUT_DIR/vals.txt"
    
    sudo /home/hannahmanuela/perf-tools/bin/perf record -o $OUT_DIR/perf.data -F 500 -g --call-graph dwarf -- sh -c "$COMMAND"

    sudo /home/hannahmanuela/perf-tools/bin/perf report -n --stdio -i $OUT_DIR/perf.data > $OUT_DIR/call_graph.txt

    echo "done with $POLICY $NUM_CORE"
done
