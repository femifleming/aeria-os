#!/bin/sh
# usage: tools/run.sh python3 /aeria/<fx>/test.py
export PATH="/Applications/Docker.app/Contents/Resources/bin:$PATH"
exec docker run --rm --network none --shm-size=2g -v "$HOME/octa/aeria-fx:/aeria" -v "$HOME/octa/aeria-fx/.sdk:/sdk" -w /aeria modwerk-source-tools "$@"
