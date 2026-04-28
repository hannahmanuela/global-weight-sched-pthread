#!/bin/bash 


if [[ $(id -u) -ne 0 ]]; then
    echo "Please run as root"
    exit 1
fi

INP_DIR=$1

/home/hannahmanuela/perf-tools/bin/perf script -i $INP_DIR/perf.data | ./graph-gen/stackcollapse-perf.pl > $INP_DIR/out.perf-folded
./graph-gen/flamegraph.pl $INP_DIR/out.perf-folded > $INP_DIR/flame.svg
# xdg-open $INP_DIR/flame.svg &
