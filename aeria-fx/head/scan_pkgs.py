"""Stock-free scan + capability read of tap.mwrm / head.mwrm (the method of ~/octa/build/fx-packages/tools/scan.py)."""
import importlib.util, sys, json, struct
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
for name in ('tap.mwrm', 'head.mwrm'):
    data = Path('/out', name).read_bytes()
    views = [data] + mp.dsp_words(data)
    longest = max([n for v in views for n in mp.runs(v, stock).values()] or [0])
    found = check_stock_free.stock_runs({'sections': {'package': {'parts': [['hex', data.hex()]]}}}, hay, 16)
    print('%-10s %5d bytes  longest shared run with stock %2d (fails at %d)  check_stock_free hits %d  -> %s' % (
        name, len(data), longest, mp.LIMIT, len(found), 'CLEAN' if longest < mp.LIMIT and not found else 'NOT CLEAN'))
    print('           capabilities', mp.capabilities(data))
    abi, flags = struct.unpack('>HH', data[4:8])
    dw, dr, eff, slots, init, proc, cyc, kind, st, buf = struct.unpack('>IHBBHHHBBH', data[32:50])
    print('           DSP: %d words, %d relocations, preferred id %d, slots %d (1 FX1), cycles %d kind %d (1 executed), '
          'state %d, buffer %d, name %r' % (dw, dr, eff, slots, cyc, kind, st, buf, data[50:66].rstrip(b'\0').decode()))
