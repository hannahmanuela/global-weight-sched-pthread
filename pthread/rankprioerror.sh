#!/bin/bash

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <ncore> <log>"
    exit 1
fi

./schedule -l /tmp/$2 -p -r 2 -b 2 -g 2 rr1 $1 $(($1 * 4))
./logmerge $1 $2
./rankerror -p $2
