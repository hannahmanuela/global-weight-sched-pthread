#!/bin/bash

if [ "$#" -ne 3 ]; then
    echo "Usage: $0 <ncore> <nheap>"
    exit 1
fi

./global-heap $1 32 $2 0
./logmerge $1 vtlog
./rankerror vtlog
