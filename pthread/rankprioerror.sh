#!/bin/bash

if [ "$#" -ne 4 ]; then
    echo "Usage: $0 <scheduler> <ncore> <nproc> <log>"
    exit 1
fi

./schedule -l /tmp/$4 -r 2 -b 2 -g 2 $1 $2 $3
#./schedule -l /tmp/$4 $1 $2 $3
# ./schedule -h 8 -l /tmp/$4 $1 $2 $3

./logmerge $2 $4
./rankerror -p $4
