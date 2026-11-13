"""Step 0: worst-case cost of each Balloon Shop effect for the package's declared cycles, with trig splits.

Uses the effects' own harness (/aeria/tools/aeria.py: the effect overlaid on the stock MAIN OS DSP payloads,
dsp_host core meter minus a null effect), with its scratch moved to the container's /tmp (the stock-payload
images never leave the container). Writes /h/.build/bs/cycles.json.
"""
import json, math, sys
from pathlib import Path
sys.path.insert(0, '/aeria/tools')
import aeria as A
A.BUILD = Path('/tmp/bsw'); A.BUILD.mkdir(exist_ok=True)
A.HOST = Path('/h/.build/dsp_host')
from remix import registry

STATUS_WORST = {'pitch': 195, 'reso': 215, 'disto': 152, 'ringph': 225, 'smear': 269, 'grains': 253}  # STATUS.md
STATE = {'pitch': 64, 'reso': 64, 'disto': 64, 'ringph': 48, 'smear': 48, 'grains': 64}               # init clears
res = {}
null = A.null_cost()['max_per_frame']
for d in ('pitch', 'reso', 'disto', 'ringph', 'smear', 'grains'):
    mod = registry._load_one(Path(f'/aeria/{d}/manifest.py'))
    names = [p.name.decode() if p.name else '' for p in mod.params]
    defaults = [p.default or 0 for p in mod.params]
    asm = next(Path(f'/aeria/{d}').glob('*.asm'))
    mems, words, init, proc = A.assemble(asm, mod.key)
    modes = range(mod.params[mod.mode_slot].count) if mod.mode_slot is not None else [None]
    worst, rows = 0.0, []
    for mode in modes:
        p = list(defaults)
        for k, v in mod.dear.items():
            p[names.index(k)] = v
        if mode is not None:
            p[mod.mode_slot] = mode
        for split in (0, 1, 7):
            blocks = 3000 if d == 'grains' else 1200
            L, R, m = A.render(f'{d}_c', mems, init, proc, p, A.noise(blocks * 16, 0.5), blocks, split=split)
            c = m['max_per_frame'] - null
            rows.append(dict(mode=mode, split=split, worst=round(c, 1), mean=round(m['mean_per_frame'] - null, 1)))
            worst = max(worst, c)
    declare = math.ceil(max(worst, STATUS_WORST[d]))
    res[d] = dict(measured_worst=round(worst, 1), status_worst=STATUS_WORST[d], declare=declare, state=STATE[d], runs=rows)
    print(d, 'measured worst %.1f (STATUS %d) -> declare %d' % (worst, STATUS_WORST[d], declare), flush=True)
Path('/h/.build/bs').mkdir(parents=True, exist_ok=True)
Path('/h/.build/bs/cycles.json').write_text(json.dumps(res, indent=1))
