"""AERIA ROSE packaging step 3 (head/bs_verify.py): read the package back: page records (as fxpage.c applies them), DSP header, stock-free scan."""
import importlib.util, json, struct, sys
from pathlib import Path
spec = importlib.util.spec_from_file_location('mp', '/work/sdk/machines/octatrack/elekloader/module_packages.py')
mp = importlib.util.module_from_spec(spec); sys.modules['mp'] = mp; spec.loader.exec_module(mp)
mp.firmware_codec.use_elekloader(Path('/up/elekloader'))
from elekloader import formats
parsed, device, _ = formats.load('/stock/stock.bin')
stock = formats.main_image(parsed, device)
sys.path.insert(0, '/work/sdk/machines/octatrack/elekloader')
import check_stock_free
hay = check_stock_free.haystacks(Path('/stock/stock.bin'), Path('/up/elekloader'))
report = json.loads(Path('/r/.build/pkg/report.json').read_text())


def page(data):
    at = data.index(b'MWPG')
    r = data[at:]
    inherited, ni, ns, nf = struct.unpack('>HBBB', r[40:45])
    d = {}                                   # P-relative byte -> value, as fxpage.c writes them
    e = 52
    for _ in range(ni):
        off, width, _, value = struct.unpack('>HBBI', r[e:e + 8]); e += 8
        for k in range(width):
            d[off + k] = (value >> (8 * (width - 1 - k))) & 0xff
    for _ in range(ns):
        off, width, length = struct.unpack('>HBB', r[e:e + 4])
        text = r[e + 4:e + 4 + length]
        for c in range(width):
            d[off + c] = text[c] if c < length else 0
        e += (4 + length + 3) & ~3
    fm = []
    for _ in range(nf):
        slot, fixups, clear, wide = r[e], r[e + 1], r[e + 2], r[e + 3]
        fm.append((slot, wide)); e += (12 + 2 * fixups + 3) & ~3
    return d, fm


def word(d, off):
    if not all(off + k in d for k in range(4)): return None
    return int.from_bytes(bytes(d[off + k] for k in range(4)), 'big')


def text(d, off, n):
    return bytes(d.get(off + k, 0) for k in range(n)).split(b'\0')[0].decode('latin1')


ok_all = True
for r in report:
    data = Path('/out', r['file']).read_bytes()
    views = [data] + mp.dsp_words(data)
    longest = max([n for v in views for n in mp.runs(v, stock).values()] or [0])
    found = check_stock_free.stock_runs({'sections': {'package': {'parts': [['hex', data.hex()]]}}}, hay, 16)
    clean = longest < mp.LIMIT and not found
    d, fm = page(data)
    en = word(d, 0x18e) or 0
    slots = []
    for k in range(12):
        name = text(d, 0x16 + 6 * k, 6)
        cnt = word(d, 0x9a + 4 * k)
        dflt = d.get(0x5e + k)
        drawn = (en >> (4 * k)) & 1 if k < 8 else ((word(d, 0x18a) or 0) >> (4 * (k - 8))) & 1
        slots.append((k, name, cnt, dflt, drawn))
    want = [n for n in r['knobs']]
    names_ok = [s[1] for s in slots] == [w for w in want] + [''] * (12 - len(want))
    counts_ok = all(s[2] == c for s, c in zip(slots, r['counts']) if c is not None)
    ok = clean and names_ok and counts_ok and text(d, 4, 5) == r['abbr'] and text(d, 9, 13) == r['full'] and d.get(3) == r['id']
    ok_all &= ok
    r.update(stock_clean=clean, longest=longest, enable='0x%08x' % en, formatter_slots=fm,
             page=[dict(slot=k, name=n, count=c, default=df, drawn=bool(dr)) for k, n, c, df, dr in slots])
    print('%-18s stock-free %s (longest run %d, hits %d)  abbr %r full %r id %d  enable %s  formatters %s  page ok %s' % (
        r['file'], 'CLEAN' if clean else 'NOT CLEAN', longest, len(found), text(d, 4, 5), text(d, 9, 13), d.get(3), r['enable'], fm, ok))
    for k, n, c, df, dr in slots:
        if n or dr: print('    slot %2d %-5s count %-4s default %-4s %s' % (k, n, c, df, 'drawn' if dr else 'hidden'))
Path('/r/.build/pkg/report.json').write_text(json.dumps(report, indent=1))
print('ALL OK' if ok_all else 'PROBLEMS')
