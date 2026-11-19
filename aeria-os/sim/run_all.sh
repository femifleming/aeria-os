#!/bin/sh
# Every simulator test against one firmware image, four at a time; a summary at the end.
#   sim/run_all.sh [MAIN.raw]      (default: build/repair/ram/MAIN.raw)
# Takes about 15-25 minutes (the simulator runs the unit at roughly a ninth of its speed).
HERE=$(cd "$(dirname "$0")" && pwd)
ARG=${1:-$HERE/../build/repair/ram/MAIN.raw}
IMG=$(cd "$(dirname "$ARG")" && pwd)/$(basename "$ARG")     # resolved from where it was called
cd "$HERE/tests" || exit 1
[ -f "$IMG" ] || { echo "no image: $IMG"; exit 1; }
[ -f ../aeria_sim ] || ../build.sh || exit 1
[ -f ../card.img ] || ../mkcard.sh || exit 1
OUT=$(mktemp -d); echo "image: $IMG"; echo "logs:  $OUT"
ls test_*.py | xargs -P 4 -I{} sh -c 'AERIA_IMAGE="$1" python3 -u "$2" > "$3/$2.log" 2>&1' _ "$IMG" {} "$OUT"
fail=0
for f in test_*.py; do
    r=$(grep '^RESULT' "$OUT/$f.log" | cut -c1-160); [ -n "$r" ] || r="RESULT $f: CRASHED (see $OUT/$f.log)"
    echo "$r"; case "$r" in *PASS) ;; *) fail=1; grep '^FAIL' "$OUT/$f.log" | cut -c1-200 | sed 's/^/    /';; esac
done
[ $fail = 0 ] && echo "ALL PASS" || echo "SOME FAILED"
exit $fail
