

SECS_TO_RUN=3
NUM_CORE=2

COMMAND="./global-heap -t $SECS_TO_RUN -g 1 $NUM_CORE $(($NUM_CORE * 5))"

echo $COMMAND

OUT_DIR=out-onegrp-test/$NUM_CORE

rm -rf $OUT_DIR
mkdir -p $OUT_DIR

sudo /home/hannahmanuela/perf-tools/bin/perf record -o $OUT_DIR/perf.data -F 500 -g -- sh -c "$COMMAND"

sudo /home/hannahmanuela/perf-tools/bin/perf report -n --stdio -i $OUT_DIR/perf.data > $OUT_DIR/call_graph.txt


