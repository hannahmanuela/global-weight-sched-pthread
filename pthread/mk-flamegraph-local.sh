#!/bin/bash

perf record -g ./schedule -y gwfs 4 64
perf script | stackcollapse-perf.pl > out.perf-folded
flamegraph.pl out.perf-folded > flamegraph.svg
