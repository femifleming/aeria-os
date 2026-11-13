#!/bin/sh
# Make a release for AERIA Flasher: build the flash image, wrap it as an OS file, and put both with notes into
# releases/<build name>/. Usage: tools/release.sh "one line of what is new" [tested]
# ("tested" = this exact build was started from RAM on the unit and checked before release)
set -e
cd "$(dirname "$0")/.."
PY=~/.cache/modwerk-upstream/venv/bin/python
NOTE="${1:-}"; TESTED="${2:-}"
(cd ../cloudfin && ./assemble.sh >/dev/null)
(cd src && FLASH=1 ./build.sh ../build/flashimg >/dev/null 2>&1) || { echo "BUILD FAILED"; exit 1; }
$PY flash/mk_flash.py --stock ~/octa/build/to-card/OCTATRACK_OS1.40C_OFFICIAL.bin --image build/flashimg/ram/MAIN.raw --out build/flash >/dev/null
NAME=$(strings build/flashimg/ram/MAIN.raw | grep -o "AERIA OS [A-Z][a-z][a-z] [ 0-9][0-9] 20[0-9][0-9] [0-9:]*" | head -1)
DIR="releases/$(echo "$NAME" | sed 's/AERIA OS //; s/ /_/g; s/:/-/g')"
mkdir -p "$DIR"
cp build/flash/AERIA_OS.bin build/flashimg/ram/MAIN.raw "$DIR/"
{ echo "name: $NAME"; echo "made: $(date '+%Y-%m-%d %H:%M')"; echo "tested_on_unit: ${TESTED:-no}"; echo "notes: $NOTE"; } > "$DIR/release.txt"
echo "$DIR"
