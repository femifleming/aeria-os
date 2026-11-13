#!/bin/sh
# Runs every AERIA gate; each writes <fx>/test_results.md. Summary at the end.
cd "$(dirname "$0")/.."
for fx in pitch reso disto ringph smear grains rose; do
  tools/run.sh python3 /aeria/$fx/test.py > .build/$fx.log 2>&1
  printf '%-7s %s\n' "$fx" "$(tail -1 .build/$fx.log)"
done
