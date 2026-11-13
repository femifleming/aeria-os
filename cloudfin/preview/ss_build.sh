#!/bin/sh
# Builds preview/aeria_ss.c = aeria12.c with parts/screensavers.c inserted just before the browser
# part (i.e. after intro), then the contact sheet, and runs it. Optional arg: one screensaver index.
cd "$(dirname "$0")/.." || exit 1
mark=$(sed -n 1p parts/browser.c)
awk -v mark="$mark" -v ss=parts/screensavers.c '
  !done && $0 == mark { while ((getline l < ss) > 0) print l; done = 1 }
  { print }' aeria12.c > preview/aeria_ss.c
grep -q "ss_start(int which)" preview/aeria_ss.c || { echo "insert failed"; exit 1; }
clang -O1 -w -I preview -I . preview/ss_sheet.c -o preview/ss_sheet && ./preview/ss_sheet "$@" && \
    sips -s format png preview/ss_sheet.pgm --out preview/ss_sheet.png >/dev/null
[ -f preview/ss_big.pgm ] && sips -s format png preview/ss_big.pgm --out preview/ss_big.png >/dev/null
