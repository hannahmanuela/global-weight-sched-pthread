#!/bin/bash

if [ "$#" -ne 3 ]; then
    echo "Usage: $0 <sched> <ncore> <log>"
    exit 1
fi

./schedule -l /tmp/$3 -p -r 2 -b 2 -g 2 $1 $2 $(($2 * 4))
./logmerge $2 $3
./rankerror -p $3
