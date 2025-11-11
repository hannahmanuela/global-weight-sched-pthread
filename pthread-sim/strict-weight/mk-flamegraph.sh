#!/bin/bash 

perf script | stackcollapse-perf.pl > out.perf-folded
flamegraph.pl out.perf-folded > flamegraph.svg
xdg-open flamegraph.svg &
