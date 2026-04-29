#!/bin/bash

if [ "$#" -ne 4 ]; then
    echo "Usage: $0 <scheduler> <ncore> <nproc> <log>"
    exit 1
fi

./schedule $1 -l /tmp/$3 -t 2 $2 $3

./logmerge $2 $4
./rankerror $4
