"""Build AERIA TAP and AERIA HEAD as USB .mwrm packages (private; never published).

Run: aeria-fx/head/run.sh python3 -B /h/package.py
(mounts: /work = the Modwerk repo read-only, /up = the Elekloader checkout, /out = ~/octa/build/fx-packages)

The repo's own tools do the work, unchanged and read from /work:
  * the relocatable DSP package: sdk/machines/octatrack/elekloader/dsp_packages.package (two-origin
    assembly, relocations, placement proofs), handed a stand-in for the Octabam module (these drafts have
    no manifest in the repo, which is read-only here);
  * the page recipe: build.py's page_section packing, fed a recipe built here in the same form as the
    published ones (scripts/octatrack-module-page.mjs reads src/engine/assets/descriptor-recipes.json, which
    has no AERIA rows). Donor = DARK REV's descriptor 0x400d58b8 and its SHA-256, as every DSP module
    (miniverb, ring-sc, character) uses; every page-1 slot is written as ring-sc writes its plain knobs;
  * sdk/runtime/loader/build.py's main (dsp_section checks, ABI 6 header), then abi9.py for HEAD
    (requires keybus, ring-sc's four read claims).
"""
import hashlib, importlib.util, json, os, struct, sys, tempfile, types
from pathlib import Path

WORK = Path('/work')
OUT = Path('/out')


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod


dp = load('dp', WORK / 'sdk/machines/octatrack/elekloader/dsp_packages.py')
bld = load('mwbuild', WORK / 'sdk/runtime/loader/build.py')
abi9 = load('abi9', WORK / 'sdk/runtime/loader/abi9.py')

DONOR = 1074616560          # 0x400d58b8 DARK REV, as in every published DSP module recipe
DONOR_SHA = '3b3f9b15f2f6addfea9fc281f5b9fdf054bf305778059b8d43db41d390f0401f'
recipes = {r['id']: r for r in json.loads((WORK / 'src/engine/assets/descriptor-recipes.json').read_text())['recipes']}
assert recipes['ring-sc']['donorAddress'] == DONOR and recipes['ring-sc']['donorSha256'] == DONOR_SHA
assert recipes['miniverb']['donorAddress'] == DONOR and recipes['miniverb']['donorSha256'] == DONOR_SHA

MODULES = {
    'aeria-tap': dict(asm='/t/tap.asm', key='AERIA_TAP', fx=0x1d, abbr='TAP', full='AERIA TAP', name='AERIA TAP',
                      params=[], slots='fx1', cycles=3, state=1, out='tap.mwrm', keybus=False),
    'aeria-head': dict(asm='/h/head.asm', key='AERIA_HEAD', fx=0x07, abbr='HEAD', full='AERIA HEAD', name='AERIA HEAD',
                       params=[('FDBK', 0), ('FTON', 118), ('SMPL', 0), ('STAR', 0), ('IN', 64), ('WARM', 0)],
                       slots='fx1', cycles=170, state=34, out='head.mwrm', keybus=True),
}
KEYBUS_CLAIMS = ['y:0:r:0x800:0x200', 'y:1:r:0x800:0x200', 'y:0:r:0x33dfe:0x202', 'y:1:r:0x3bdfe:0x202']  # ring-sc's


def recipe(mid, m):
    """A page recipe in descriptor-recipes.json's form: plain knobs only (no formatter code)."""
    ints = [dict(offset=3, width=1, value=m['fx'])]
    for k, (_, default) in enumerate(m['params']):
        ints.append(dict(offset=94 + k, width=1, value=default))
    for k in range(12):                       # every slot's formatter words cleared: plain numbers
        ints += [dict(offset=202 + 4 * k, width=4, value=0), dict(offset=250 + 4 * k, width=4, value=0)]
    for k in range(len(m['params'])):         # page-1 slots: minimum 0, 128 steps (ring-sc's DPTH/KLPF/ATK/HOLD/REL)
        ints += [dict(offset=106 + 4 * k, width=4, value=0), dict(offset=154 + 4 * k, width=4, value=128)]
    ints += [dict(offset=394, width=4, value=0),        # fxpage.c: 0x18a enables slots 8-11, 0x18e slots 0-7
             dict(offset=398, width=4, value=int('0' + '1' * len(m['params']), 16))]   # a nibble a knob (miniverb 0x111111)
    strs = [dict(offset=4, width=5, value=m['abbr']), dict(offset=9, width=13, value=m['full'])]
    for k in range(12):
        strs.append(dict(offset=22 + 6 * k, width=6, value=m['params'][k][0] if k < len(m['params']) else ''))
    assert len(m['abbr']) <= 4 and len(m['full']) <= 12 and all(len(n) <= 4 for n, _ in m['params'])
    return dict(id=mid, donor=DONOR, donorSha256=DONOR_SHA, integers=ints, strings=strs, inheritedEnable=[], formatters=[])


def page_section_from(page):
    """build.py's page_section, from a recipe dict instead of the node script's output (same packing)."""
    inherited = sum(1 << slot for slot in page['inheritedEnable'])
    head = bld.PAGE_MAGIC + struct.pack('>I', page['donor']) + bytes.fromhex(page['donorSha256']) + struct.pack(
        '>HBBBBHI', inherited, len(page['integers']), len(page['strings']), len(page['formatters']), 0, 0, 0)
    body = b''.join(struct.pack('>HBBI', f['offset'], f['width'], 0, f['value']) for f in page['integers'])
    for f in page['strings']:
        text = f['value'].encode('latin-1')
        entry = struct.pack('>HBB', f['offset'], f['width'], len(text)) + text
        body += entry + b'\0' * (-len(entry) % 4)
    assert not page['formatters']
    return bytes(head + body), []


# a check that the packing above is build.py's: ring-sc's own recipe, through the node script vs through here
def check_packing():
    import subprocess
    for mid in ('everb', 'airwindows-chorus', 'lofi2', 'modulation', 'bbd', 'miniverb'):
        r = subprocess.run(['node', str(WORK / 'scripts/octatrack-module-page.mjs'), mid], capture_output=True, text=True)
        if r.returncode:
            raise SystemExit('page script failed for %s: %s' % (mid, r.stderr.strip()[-300:]))
        page = json.loads(r.stdout)
        if page['formatters']:
            continue
        mine = page_section_from(page)[0]
        theirs = bld.page_section(mid)[0]
        assert mine == theirs, 'page packing differs from build.py'
        print('page packing check: %s (no formatters) packs identically here and in build.py page_section' % mid)
        return
    raise SystemExit('no formatter-free recipe to check the packing against')


def main():
    check_packing()
    with tempfile.TemporaryDirectory() as work:
        sdk = Path(work) / 'sdk'
        for mid, m in MODULES.items():
            (sdk / 'modules' / mid).mkdir(parents=True)
            src = Path(m['asm']).read_text()
            (sdk / 'modules' / mid / Path(m['asm']).name).write_text(src)
            (sdk / 'modules' / mid / 'manifest.py').write_text('# private AERIA draft: %s (no Octabam manifest)\n' % mid)
            stand_in = types.SimpleNamespace(
                key=m['key'], author='femi',
                menu=types.SimpleNamespace(fx2_id=m['fx']),
                dsp=types.SimpleNamespace(ptable=None, split_ptable=False, asm='modules/%s/%s' % (mid, Path(m['asm']).name)))
            dp.SDK = sdk
            pkg = dp.package(types.SimpleNamespace(modules={mid: stand_in}), mid, '0.1.0-private', work)
            jpath = Path(work) / ('%s.json' % mid)
            jpath.write_text(json.dumps(dict(packages=[pkg])))
            page = recipe(mid, m)
            bld.page_section = lambda _id, page=page: page_section_from(page)
            out6 = Path(work) / ('%s.abi6.mwrm' % mid)
            sys.argv = ['build.py', '--dsp', '%s:%s' % (jpath, mid), '--slots', m['slots'], '--cycles', str(m['cycles']),
                        '--cycles-kind', 'executed', '--state', str(m['state']), '--name', m['name'], '-o', str(out6)]
            bld.main()
            data = out6.read_bytes()
            if m['keybus']:
                import subprocess
                final = OUT / m['out']
                r = subprocess.run([sys.executable, '-B', str(WORK / 'sdk/runtime/loader/abi9.py'), str(out6), '-o', str(final),
                                    '--requires', 'keybus'] + sum((['--claim', c] for c in KEYBUS_CLAIMS), []),
                                   capture_output=True, text=True)
                if r.returncode:
                    raise SystemExit(r.stdout + r.stderr)
            else:
                final = OUT / m['out']
                final.write_bytes(data)
            data = final.read_bytes()
            abi = struct.unpack('>H', data[4:6])[0]
            print('%-11s %s: %d bytes, ABI %d, sha256 %s, %d DSP words, %d relocations, fx id 0x%02x, init %d proc %d' % (
                mid, final, len(data), abi, hashlib.sha256(data).hexdigest(), pkg['words'], len(pkg['relocations']),
                pkg['fxId'], pkg['init'], pkg['proc']))



