#!/bin/bash

q_opt=""
while getopts "q" opt; do
    case "$opt" in
        q) q_opt="-q" ;;
        *) echo "Usage: $0 [-q] <sched> <ncore> <log>"; exit 1 ;;
    esac
done
shift $((OPTIND - 1))

if [ "$#" -ne 3 ]; then
    echo "Usage: $0 [-q] <sched> <ncore> <log>"
    exit 1
fi

./schedule -l /tmp/$3 -p $q_opt -r 2 -b 2 -g 2 $1 $2 $(($2 * 4))
./logmerge $2 $3
./rankerror -p $3
