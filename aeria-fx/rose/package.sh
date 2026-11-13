#!/bin/sh
# Builds ~/octa/build/fx-packages/aeria-rose.mwrm. Run rose/test.py first (it writes .cost.json).
cd "$(dirname "$0")" && mkdir -p .build/pkg && \
./run_pkg.sh sh -c 'python3 -B /r/pkg/recipe.py && python3 -B /r/pkg/build.py && python3 -B /r/pkg/verify.py'
