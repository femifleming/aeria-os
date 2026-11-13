#!/bin/sh
# usage: tools/asm.sh <fx>/<name>.asm   -- raw dsp_asm with its diagnostics
exec "$(dirname "$0")/run.sh" sh -c "/opt/toolchain/vendor/dsp56300/build/source/dsp_host/dsp_asm -in /aeria/$1 -org 2000 -out /tmp/o.bin -sym /tmp/o.sym -list 2>&1 | grep -iv '^ *[0-9a-f]\{4,5\} ' | tail -20"
