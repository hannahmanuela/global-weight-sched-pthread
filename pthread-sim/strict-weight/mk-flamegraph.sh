#!/bin/bash 

perf script | stackcollapse-perf.pl > out.perf-folded
flamegraph.pl out.perf-folded > $1.svg
xdg-open $1.svg &
