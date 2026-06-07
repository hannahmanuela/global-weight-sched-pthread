#!/bin/bash

perf record -g ./test-mheap 4
perf script | stackcollapse-perf.pl > out.perf-folded
flamegraph.pl out.perf-folded > flamegraph.svg
