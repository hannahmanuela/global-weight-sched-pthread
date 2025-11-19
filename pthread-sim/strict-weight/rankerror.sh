#!/bin/bash

if [ "$#" -ne 3 ]; then
    echo "Usage: $0 <ncore> <nproc> <nheap>"
    exit 1
fi

./global-heap $1 $2 $3 0
./logmerge $1 vtlog
./rankerror vtlog
