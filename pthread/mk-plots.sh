#!/bin/bash

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <dir>"
    exit 1
fi

./plot-tp.py $1
gnuplot $1/plot-tp.gp
./plot-rank.py $1
gnuplot $1/plot-rank.gp
