"""Step 2: the six Balloon Shop effects as USB .mwrm packages (private; never published).

Run after bs_recipes.py and bs_cycles.py:  head/run_bs.sh python3 -B /h/bs_package.py

* DSP: sdk/machines/octatrack/elekloader/dsp_packages.package (two-origin relocation, placement proofs) on the
  generated .asm, unchanged, from /aeria (read only).
* Page: scripts/octatrack-module-page.mjs, the repo's own composer, run from a disposable copy of src/engine
  and scripts/ whose descriptor-recipes.json / menu-recipes.json carry the rows bs_recipes.py made from the
  manifests (formatter code for the labelled selects, WIDE dials, MODE renames), then build.py's page_section.
* Package: build.py main (ABI 6), cycles from /h/.build/bs/cycles.json.
"""
import hashlib, importlib.util, json, math, os, shutil, struct, subprocess, sys, tempfile, types
from pathlib import Path

WORK, OUT = Path('/work'), Path('/out')


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec); sys.modules[name] = mod; spec.loader.exec_module(mod)
    return mod


dp = load('dp', WORK / 'sdk/machines/octatrack/elekloader/dsp_packages.py')
bld = load('mwbuild', WORK / 'sdk/runtime/loader/build.py')
rec = json.loads(Path('/h/.build/bs/recipes.json').read_text())
cyc = json.loads(Path('/h/.build/bs/cycles.json').read_text())

# a disposable copy of the page composer with the AERIA rows added
tmp = Path(tempfile.mkdtemp())
def mirror(src, dst, keep):
    """dst mirrors src by symlinks, except the path `keep` (a list of names down to the copied directory)."""
    dst.mkdir(parents=True, exist_ok=True)
    for child in src.iterdir():
        if keep and child.name == keep[0]:
            if len(keep) == 1:
                shutil.copytree(child, dst / child.name)
            else:
                mirror(child, dst / child.name, keep[1:])
        else:
            os.symlink(child, dst / child.name)
mirror(WORK, tmp, ['src', 'engine', 'assets'])
os.unlink(tmp / 'scripts'); (tmp / 'scripts').mkdir()
shutil.copy(WORK / 'scripts/octatrack-module-page.mjs', tmp / 'scripts/')
for name, key, rows in (('descriptor-recipes.json', 'recipes', rec['descriptors']), ('menu-recipes.json', 'recipes', rec['menus'])):
    p = tmp / 'src/engine/assets' / name
    doc = json.loads(p.read_text())
    doc[key] = [r for r in doc[key] if not r['id'].startswith('aeria-')] + rows
    p.write_text(json.dumps(doc))


def page_section(module_id):
    out = subprocess.run(['node', str(tmp / 'scripts/octatrack-module-page.mjs'), module_id], capture_output=True, text=True)
    if out.returncode:
        raise SystemExit('page %s: %s' % (module_id, out.stderr[-800:]))
    page = json.loads(out.stdout)
    real = subprocess.run
    bld.subprocess = types.SimpleNamespace(run=lambda *a, **k: types.SimpleNamespace(returncode=0, stdout=out.stdout, stderr=''))
    try:
        return orig_page_section(module_id)
    finally:
        bld.subprocess = subprocess


orig_page_section = bld.page_section
# sanity: the copy composes a published page byte-identically to the repo's script
assert page_section('ring-sc') == orig_page_section('ring-sc'), 'composer copy differs'
print('composer copy: ring-sc page identical to the repo script\'s')
bld.page_section = page_section

report = []
with tempfile.TemporaryDirectory() as work:
    for row in rec['table']:
        mid = row['pkg']
        sdk = Path(work) / 'sdk'
        (sdk / 'modules' / mid).mkdir(parents=True)
        asm = Path(row['asm'])
        (sdk / 'modules' / mid / asm.name).write_text(asm.read_text())
        shutil.copy(Path('/aeria') / row['dir'] / 'manifest.py', sdk / 'modules' / mid / 'manifest.py')
        stand_in = types.SimpleNamespace(key=mid.upper().replace('-', '_'), author='femi', menu=types.SimpleNamespace(fx2_id=row['id']),
                                         dsp=types.SimpleNamespace(ptable=None, split_ptable=False, asm='modules/%s/%s' % (mid, asm.name)))
        dp.SDK = sdk
        pkg = dp.package(types.SimpleNamespace(modules={mid: stand_in}), mid, '0.1.0-private', work)
        jp = Path(work) / (mid + '.json'); jp.write_text(json.dumps(dict(packages=[pkg])))
        slots = 'fx2' if row['buffer'] > 3072 else ('fx1' if row['fx1_only'] else 'both')
        cycles = cyc[row['dir']]['declare']
        out = OUT / row['out']
        sys.argv = ['build.py', '--dsp', '%s:%s' % (jp, mid), '--slots', slots, '--cycles', str(cycles), '--cycles-kind', 'executed',
                    '--state', str(cyc[row['dir']]['state']), '--name', row['full']] + (['--buffer', str(row['buffer'])] if row['buffer'] else []) + ['-o', str(out)]
        bld.main()
        data = out.read_bytes()
        report.append(dict(file=row['out'], sha256=hashlib.sha256(data).hexdigest(), bytes=len(data), abi=struct.unpack('>H', data[4:6])[0],
                           slots=slots, id=row['id'], cycles=cycles, measured=cyc[row['dir']], words=pkg['words'], relocations=len(pkg['relocations']),
                           buffer=row['buffer'], state=cyc[row['dir']]['state'], abbr=row['abbr'], full=row['full'], knobs=row['knobs'],
                           counts=row['counts'], defaults=row['defaults'], modeViews=row['modeViews']))
Path('/h/.build/bs/report.json').write_text(json.dumps(report, indent=1))
for r in report:
    print('%-18s %6d B ABI %d %-4s id 0x%02x cycles %d buffer %d state %d sha %s' % (r['file'], r['bytes'], r['abi'], r['slots'], r['id'],
          r['cycles'], r['buffer'], r['state'], r['sha256'][:16]))
