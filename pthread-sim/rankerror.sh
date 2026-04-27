#!/bin/bash

if [ "$#" -ne 3 ]; then
    echo "Usage: $0 <ncore> <nproc> <log>"
    exit 1
fi

./global-heap -2 -s -l /tmp/$3 -t 2 $1 $2
./logmerge $1 $3
./rankerror $3
