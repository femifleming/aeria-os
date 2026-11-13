#!/bin/sh
# Cross-compile check (Docker): aeria12.c with screensavers inserted, as is (API unused, so the
# compiler drops it) and with a probe that calls the API (the real cost once ui12 wires it in).
cd "$(dirname "$0")/.." || exit 1
export PATH="/Applications/Docker.app/Contents/Resources/bin:$PATH"
D=$(mktemp -d)
./preview/ss_build.sh 1 >/dev/null || exit 1
cp preview/aeria_ss.c $D/aeria.c; cp logo.inc $D/
{ cat preview/aeria_ss.c; echo 'void ss_probe(void); void ss_probe(void) { F = (uint32_t *)0; ss_start(1); ss_tick(); ss_draw(); ss_key(0x34, 1); ss_enc(0, 1); (void)ss_name; }'; } > $D/probe.c
docker run --rm --network none -v ~/octa/modwerk:/repo:ro -v $D:/src:ro -w /repo modwerk-source-tools sh -c \
  'cp /src/aeria.c /tmp/aeria.c && cp /src/probe.c /tmp/probe.c && cp /src/logo.inc /tmp/ && python3 -B sdk/runtime/loader/build.py /tmp/aeria.c -o /tmp/x.mwrm && python3 -B sdk/runtime/loader/build.py /tmp/probe.c -o /tmp/p.mwrm' 2>&1 | grep -v "^$" | tail -25
rm -rf $D
