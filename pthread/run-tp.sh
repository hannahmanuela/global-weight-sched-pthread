#!/bin/bash

SCHEDULERS=("rr" "gwfs" "gq" "pcrq")

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <ncore>"
    exit 1
fi

echo "----low load"

for s in ${SCHEDULERS[@]}; do
    ./schedule -y $s $1 $1
done

echo "----high load"

for s in ${SCHEDULERS[@]}; do
    ./schedule -y $s $1 $(($1 * 4))
done
