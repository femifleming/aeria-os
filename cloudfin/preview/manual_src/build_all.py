#!/usr/bin/env python3
"""Builds every AERIA OS document: the manual and the quick guide, in English, Chinese, Japanese and German, each in
the dark and the light theme (16 PDFs), into aeria-os/manual/<lang>/, plus the English dark ones at the top of
aeria-os/manual/ as before. A language whose body.<lang>.html or quick.<lang>.html is not there yet is skipped,
and said so. The page counts and the build's checks (lines starting CHECK) are printed at the end.

Usage: build_all.py [-j N] [--lang en,de] [--theme dark] [--only manual|quick]
  -j N      N builds at a time (default 1); each build has its own files, so they can run side by side"""
import os, sys, re, subprocess, time
from concurrent.futures import ThreadPoolExecutor

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import lang

man_dir = os.path.expanduser('~/octa/aeria-os/manual')


def arg(name, default):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default


def main():
    langs = arg('--lang', ','.join(lang.LANGS)).split(',')
    themes = arg('--theme', 'dark,light').split(',')
    kinds = arg('--only', 'manual,quick').split(',')
    jobs = int(arg('-j', '1'))
    todo, skipped = [], []
    for code in langs:
        for kind in kinds:
            src = os.path.join(here, f'{"body" if kind == "manual" else "quick"}{"" if code == "en" else "." + code}.html')
            if not os.path.exists(src):
                skipped.append(f'{lang.NAMES[code]} {kind}: {os.path.basename(src)} is not there yet: skipped')
                continue
            for t in themes:
                todo.append((code, kind, t))
    for s in skipped:
        print('SKIP', s)

    def run(job):
        code, kind, t = job
        script = 'build_manual.py' if kind == 'manual' else 'build_quick.py'
        t0 = time.time()
        p = subprocess.run([sys.executable, os.path.join(here, script), '--pdf', '--lang', code, '--theme', t], capture_output=True, text=True)
        out = p.stdout + p.stderr
        name = ('AERIA_OS_Manual' if kind == 'manual' else 'AERIA_OS_Quick_Guide') + f'_{t.capitalize()}.pdf'
        pdf = os.path.join(man_dir, code, name)
        pages = '?'
        if p.returncode == 0 and os.path.exists(pdf):
            info = subprocess.run(['pdfinfo', pdf], capture_output=True, text=True).stdout
            m = re.search(r'Pages:\s+(\d+)', info); pages = m.group(1) if m else '?'
        status = 'ok' if p.returncode == 0 else 'FAILED'
        print(f'{status:6} {code} {kind:6} {t:5} {pages:>4} pages  {time.time() - t0:5.0f} s', flush=True)
        return job, p.returncode, pdf, pages, out

    with ThreadPoolExecutor(max_workers=jobs) as ex:
        results = list(ex.map(run, todo))

    print('\nThe documents:')
    for (code, kind, t), rc, pdf, pages, out in results:
        print(f'  {pdf if rc == 0 else "FAILED: " + code + " " + kind + " " + t}  {pages} pages')
    for top in ('AERIA_OS_Manual.pdf', 'AERIA_OS_Quick_Guide.pdf'):
        f = os.path.join(man_dir, top)
        if os.path.exists(f):
            print(f'  {f} (English, dark: as before)')
    checks = []
    for (code, kind, t), rc, pdf, pages, out in results:
        for l in out.splitlines():
            if l.startswith('CHECK') or (rc and l.strip()) or 'Error' in l or 'SystemExit' in l:
                checks.append(f'[{code} {kind} {t}] {l}')
    if checks:
        print('\nChecks:')
        seen = set()
        for c in checks:
            key = re.sub(r'^\[(\w+) (\w+) \w+\]', r'[\1 \2]', c)
            if key not in seen:              # the same in both themes is said once
                seen.add(key); print(' ', c)
    for s in skipped:
        print('  SKIPPED', s)
    if any(rc for _, rc, _, _, _ in results):
        sys.exit(1)


if __name__ == '__main__':
    main()
