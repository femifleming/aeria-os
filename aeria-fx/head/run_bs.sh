#!/bin/sh
# Balloon Shop packaging runner: /aeria (read), /sdk (read), /h (this folder), /work repo ro, /out packages.
export PATH="/Applications/Docker.app/Contents/Resources/bin:$PATH"
exec docker run --rm --network none --shm-size=2g \
  -v "$HOME/octa/aeria-fx:/aeria:ro" -v "$HOME/octa/aeria-fx/.sdk:/sdk:ro" -v "$HOME/octa/aeria-fx/head:/h" \
  -v "$HOME/octa/modwerk:/work:ro" -v "$HOME/.cache/modwerk-upstream:/up:ro" \
  -v "$HOME/octa/build/fx-packages:/out" -v "$HOME/octa/build:/stock:ro" \
  -e HOME=/tmp -e ELEKLOADER_UPSTREAM=/up/elekloader -e PYTHONDONTWRITEBYTECODE=1 \
  -e ELEKLOADER_DSP_ASM=/opt/toolchain/vendor/dsp56300/build/source/dsp_host/dsp_asm \
  -w /work modwerk-source-tools "$@"
