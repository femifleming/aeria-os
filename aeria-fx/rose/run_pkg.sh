#!/bin/sh
# AERIA ROSE packaging runner (the head/run_bs.sh mounts, with /r = this folder):
#   rose/run_pkg.sh python3 -B /r/package.py
# /aeria and /sdk read only, /work = the Modwerk repo read only, /out = ~/octa/build/fx-packages.
export PATH="/Applications/Docker.app/Contents/Resources/bin:$PATH"
exec docker run --rm --network none --shm-size=2g \
  -v "$HOME/octa/aeria-fx:/aeria:ro" -v "$HOME/octa/aeria-fx/.sdk:/sdk:ro" -v "$HOME/octa/aeria-fx/rose:/r" \
  -v "$HOME/octa/modwerk:/work:ro" -v "$HOME/.cache/modwerk-upstream:/up:ro" \
  -v "$HOME/octa/build/fx-packages:/out" -v "$HOME/octa/build:/stock:ro" \
  -e HOME=/tmp -e ELEKLOADER_UPSTREAM=/up/elekloader -e PYTHONDONTWRITEBYTECODE=1 \
  -e ELEKLOADER_DSP_ASM=/opt/toolchain/vendor/dsp56300/build/source/dsp_host/dsp_asm \
  -w /work modwerk-source-tools "$@"
