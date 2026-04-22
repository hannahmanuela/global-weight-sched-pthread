#!/bin/bash

MV_FLAG=""
if [ "$1" = "--mv" ]; then
    MV_FLAG="-m"
    shift
fi

if [ "$#" -ne 3 ]; then
    echo "Usage: $0 [--mv] <ncore> <nproc> <log>"
    exit 1
fi

./global-heap -s $MV_FLAG -l /tmp/$3 -h 8 -t 2 $1 $2
./logmerge $1 $3
./rankerror $3
