#!/bin/bash 


if [[ $(id -u) -ne 0 ]]; then
    echo "Please run as root"
    exit 1
fi

INP_DIR=$1

for OUT_DIR in $INP_DIR/*; do
    if [ ! -d $OUT_DIR ]; then
        continue
    fi
    echo "mk-flamegraph.sh $OUT_DIR"
    sudo ./mk-flamegraph.sh $OUT_DIR
done


if [ -d $INP_DIR/2 ]; then
    for OUT_DIR in $INP_DIR/*; do
        # check if is a dir, and if not out/2 itself
        if [ ! -d $OUT_DIR ] || [ $OUT_DIR == "$INP_DIR/2" ]; then
            continue
        fi
        echo "mk diff with $INP_DIR/2 and $OUT_DIR"
        sudo ./graph-gen/difffolded.pl $INP_DIR/2/out.perf-folded $OUT_DIR/out.perf-folded | ./graph-gen/flamegraph.pl > $OUT_DIR/diff-with-2.svg
    done
fi