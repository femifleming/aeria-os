#!/bin/sh
# usage: aeria-fx/head/run.sh python3 -B /h/test_head.py   (or /t/test_tap.py, /h/package.py)
# Mounts: /h = aeria-fx/head, /t = aeria-fx/tap, /work = the Modwerk repo READ-ONLY,
# /up = the Elekloader checkout read-only, /out = ~/octa/build/fx-packages (packages only).
export PATH="/Applications/Docker.app/Contents/Resources/bin:$PATH"
exec docker run --rm --network none \
  -v "$HOME/octa/aeria-fx/head:/h" -v "$HOME/octa/aeria-fx/tap:/t" \
  -v "$HOME/octa/modwerk:/work:ro" -v "$HOME/.cache/modwerk-upstream:/up:ro" \
  -v "$HOME/octa/build/fx-packages:/out" -v "$HOME/octa/build:/stock:ro" \
  -e HOME=/tmp -e ELEKLOADER_UPSTREAM=/up/elekloader \
  -e ELEKLOADER_DSP_ASM=/opt/toolchain/vendor/dsp56300/build/source/dsp_host/dsp_asm \
  -w /work modwerk-source-tools "$@"
