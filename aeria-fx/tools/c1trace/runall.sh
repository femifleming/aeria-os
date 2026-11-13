#!/bin/sh
# runs the trace scenarios in parallel; results in r_*.out
cd "$(dirname "$0")"
BIN=${BIN:-./c1trace}
run() { $BIN "$@" > "r_$(echo "$*" | tr ' :' '__').out" 2>&1; }
run repro 11 3 & run repro 12 3 & run repro-fast 13 3 & run cycle 14 3 &
run solo:pt2399 15 3 0 & run solo:tape 16 3 1 & run solo:bbd 17 3 0 &
wait
grep -h -v "^Jit\|^m4\|^after\|^operands" r_*.out
