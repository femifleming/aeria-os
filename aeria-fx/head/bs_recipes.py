"""Step 1 of the Balloon Shop packaging: page recipes from each manifest, by the repo's own generator logic.

Runs in modwerk-source-tools with /aeria = ~/octa/aeria-fx, /sdk = its private octabam copy (read only here).
Writes /h/.build/bs/recipes.json: descriptor recipes (scripts/build-module-packages.py lines 562-603, copied
rule for rule) and menu recipes (lines 504-517) for the six effects, with the AERIA names and ids.
"""
import json, os, sys
from pathlib import Path

os.chdir('/sdk')
sys.path[:0] = ['/sdk/tools', '/sdk/tools/build']
import toolpath  # noqa
from remix import registry
from remix.schema import Remix
registry.remix = lambda _: Remix(name='aeria-pack', doc='AERIA packaging.', modules=('MINIVERB',), fallback='NONE')
import build_bus as native   # noqa: E402  (P_* offsets, plain_slots, penable)
import mode_names            # noqa: E402

FX = json.loads(Path('/h/bs_table.json').read_text())
out = dict(descriptors=[], menus=[])
for row in FX:
    module = registry._load_one(Path('/aeria') / row['dir'] / 'manifest.py')
    fid = row['id']
    integers, strings = [], []
    def integer(offset, size, value): integers.append({'offset': offset, 'width': size, 'value': value})
    def string(offset, maximum, value): strings.append({'offset': offset, 'width': maximum, 'value': value.decode('latin1')})
    integer(native.P_ID_BYTE, 1, fid)
    string(native.P_ABBR, 5, row['abbr'].encode()); string(native.P_FULLNAME, 13, row['full'].encode())
    for slot, param in enumerate(module.params):
        if param.name is not None: string(0x16 + slot * 6, 6, param.name)
        if param.default is not None: integer(0x5e + slot, 1, param.default)
    if module.stepped_slots:
        for slot in range(12):
            integer(0xca + slot * 4, 4, 0); integer(0xfa + slot * 4, 4, 0)
    else:
        for slot in native.plain_slots(module):
            integer(0xca + slot * 4, 4, 0); integer(0xfa + slot * 4, 4, 0)
    for slot in module.stepped_slots:
        integer(0xca + slot * 4, 4, 0x4003c718); integer(0xfa + slot * 4, 4, 0x40047254 if module.params[slot].count is not None and module.params[slot].count <= 5 else 0); integer(0x12a + slot * 4, 4, 0)
    for slot in module.bipolar_slots:
        integer(0xca + slot * 4, 4, 0x4003c7a0); integer(0xfa + slot * 4, 4, 0); integer(0x12a + slot * 4, 4, 0x400328e4)
    for slot, param in enumerate(module.params):
        if param.count is not None: integer(0x9a + slot * 4, 4, param.count); integer(0x6a + slot * 4, 4, 0)
        elif param.active:   # AERIA addition: every drawn plain knob states 128 steps from 0 (as ring-sc's manifest does),
            integer(0x9a + slot * 4, 4, 128); integer(0x6a + slot * 4, 4, 0)   # so no count is the donor's
    for slot, param in enumerate(module.params):
        for offset, value in [(0xca, param.formatter_word), (0xfa, param.widget_word), (0x12a, param.word_12a)]:
            if value is None: continue
            if not isinstance(value, int): raise SystemExit('raw symbol pointers are not supported here')
            integer(offset + slot * 4, 4, value)
    lo, hi = native.penable(module.active_params, module.linked_params)
    integer(native.P_PENABLE_LO, 4, lo); integer(native.P_PENABLE_HI, 4, hi)
    assert not module.menu.stock_dsp
    out['descriptors'].append(dict(id=row['pkg'], key=module.key, author=module.author, fxId=fid,
                                   donorAddress=module.menu.donor_desc + 0x38,
                                   donorSha256='3b3f9b15f2f6addfea9fc281f5b9fdf054bf305778059b8d43db41d390f0401f',
                                   integers=integers, strings=strings))
    for slot, param in enumerate(module.params):
        if not param.prints_labels: continue
        views = module.name_views_for(slot)
        names = mode_names.complete(module, slot, views) if views else {}
        if slot == module.mode_slot: names = mode_names.with_selfname(names, slot, param.labels)
        encoded = {str(value): {str(key): label.decode('latin1') for key, label in r.items()} for value, r in names.items()}
        out['menus'].append({'id': row['pkg'], 'key': module.key, 'author': module.author, 'slot': slot,
                             'name': param.name.decode('latin1'), 'labels': list(param.labels), 'renames': encoded,
                             'wideMaximum': param.count - 1 if slot in module.wide_stepped_slots else None})
    # knob table for the report
    row['knobs'] = [(p.name.decode() if p.name else '') for p in module.params]
    row['counts'] = [p.count if p.count is not None else (128 if p.active else None) for p in module.params]
    row['defaults'] = [p.default for p in module.params]
    row['buffer'] = module.claims.buffer_words if module.claims and module.claims.stock_instance_buffer else 0
    row['fx1_only'] = bool(getattr(module.claims, 'fx1_only', False))
    row['modeViews'] = [dict(mode=v.mode, names={k: n.decode() for k, n in v.names.items()}, defaults=v.defaults)
                        for v in module.mode_views]
    row['asm'] = str(Path('/aeria') / module.dsp.asm)
out['table'] = FX
Path('/h/.build/bs').mkdir(parents=True, exist_ok=True)
Path('/h/.build/bs/recipes.json').write_text(json.dumps(out, indent=1))
for d in out['descriptors']:
    print(d['id'], len(d['integers']), 'integers', len(d['strings']), 'strings')
for m in out['menus']:
    print(' menu', m['id'], 'slot', m['slot'], m['name'], len(m['labels']), 'labels', 'renames' if m['renames'] else '', 'wide' if m['wideMaximum'] else '')
