"""The Octatrack MKII panel drawings for the AERIA OS manual, as inline SVG. Our own drawing: positions measured
from a photograph of the unit, flat style, in the colours of the theme in use (theme.py; dark: a dark panel, lighter
keys, white lettering, a lit control white with an orange ring; light: a light grey panel, darker keys, a lit control
black with an orange ring). No Elektron artwork. Reload this module after theme.use() (build_manual.set_theme does).

    front()                     the full panel with numbered callouts (the list under it is in body.html)
    rear()                      the back, with numbered callouts
    mini(spec, lamps=None)      the same panel with only some controls lit, each optionally tagged with what it is
                                called in that context:  mini('FUNC MIDI LFO=MOD TRIG1-8=pads KNOBS')
    lamps(levels, tags=None)    the panel at night: each named lamp at a brightness 0..3
    trig_row(levels, title)     a strip of the sixteen trigs with their lamps (0 dark, 1 beat mark, 2 note, 3 playhead)

Control names: HPVOL MIDI REC1 REC2 REC3 PROJ PART AED MIX ARR FUNC CUE PTN BANK YES NO UP LEFT DOWN RIGHT
T1..T8 SCREEN SRC AMP LFO FX1 FX2 REC PLAY STOP LEVEL TEMPO A B C D E F SCENEA SCENEB XFADER TRIG1..TRIG16 PAGE CARD
Groups: TRIGS TRIG1-8 (any range) T1-8 (any range) KNOBS ARROWS PAGES TRANSPORT RECS SCENES"""
import re, os, sys, unicodedata
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import theme

W, H = 1585, 820          # the panel, in the drawing's own units (about 0.215 mm each)
PANEL, PANEL_EDGE = theme.P_PANEL, theme.P_PANEL_EDGE
KEY, KEY_TXT = theme.P_KEY, theme.P_KEY_TXT
DIM_PANEL, DIM_KEY, DIM_TXT = theme.P_DIM_PANEL, theme.P_DIM_KEY, theme.P_DIM_TXT
HI, HI_TXT, HI_RING = theme.P_HI, theme.P_HI_TXT, theme.P_HI_RING
INK = theme.INK              # call-out lines, badges
ON_INK = theme.BG            # a badge's number
CALL = theme.ACC             # the call-outs' frames and leaders: orange, so they never read as lettering
DEEP = theme.P_DEEP          # holes in the panel: the fader's slot, the screen's bezel, sockets
FONT = 'font-family="Helvetica Neue, Helvetica, Arial, sans-serif"'

C = {}   # name -> dict(kind, x, y (centre), w, h, label)


def _add(name, kind, x, y, w, h, label=None):
    C[name] = dict(kind=kind, x=x, y=y, w=w, h=h, label=name if label is None else label)


_add('HPVOL', 'knob', 105, 151, 64, 64, '')
_add('MIDI', 'key', 70, 254, 56, 56)
for i, n in enumerate(['REC1', 'REC2', 'REC3']):
    _add(n, 'key', 220 + i * 74.5, 225, 56, 56)
for i, n in enumerate(['PROJ', 'PART', 'AED', 'MIX', 'ARR']):
    _add(n, 'key', 70 + i * 74.75, 346, 56, 56)
_add('FUNC', 'key', 86, 468, 84, 58)
_add('CUE', 'key', 221, 468, 84, 58)
_add('PTN', 'key', 86, 580, 84, 58)
_add('BANK', 'key', 221, 580, 84, 58)
_add('YES', 'key', 336, 505, 56, 56)
_add('NO', 'key', 336, 580, 56, 56)
_add('UP', 'key', 486, 505, 56, 56, '^')
_add('LEFT', 'key', 411, 580, 56, 56, '<')
_add('DOWN', 'key', 486, 580, 56, 56, 'v')
_add('RIGHT', 'key', 561, 580, 56, 56, '>')
for i in range(4):
    _add(f'T{i + 1}', 'key', 514, 152 + i * 74.7, 56, 58)
    _add(f'T{i + 5}', 'key', 986, 152 + i * 74.7, 56, 58)
_add('SCREEN', 'screen', 750.5, 263.5, 345, 263, '')
for i, n in enumerate(['SRC', 'AMP', 'LFO', 'FX1', 'FX2']):
    _add(n, 'key', 601 + i * 74.25, 446, 56, 56)
_add('REC', 'key', 705, 580, 82, 52, 'o')
_add('PLAY', 'key', 794, 580, 82, 52, '|>')
_add('STOP', 'key', 882, 580, 82, 52, '[]')
_add('CARD', 'led', 1005, 58, 14, 14, '')
_add('LEVEL', 'knob', 1135, 199, 66, 66, '')
_add('TEMPO', 'key', 1136, 326, 58, 56, 'tempo')
for i, n in enumerate('ABC'):
    _add(n, 'knob', 1261 + i * 126, 199, 66, 66, '')
for i, n in enumerate('DEF'):
    _add(n, 'knob', 1261 + i * 126, 326, 66, 66, '')
_add('SCENEA', 'key', 1053, 543, 88, 88, 'A')
_add('SCENEB', 'key', 1503, 543, 88, 88, 'B')
_add('XFADER', 'fader', 1278, 543, 262, 110, '')
for i in range(16):
    _add(f'TRIG{i + 1}', 'key', 86 + i * 88.47, 698, 80, 80, str(i + 1))
_add('PAGE', 'key', 1502, 701, 80, 60)

GROUPS = {
    'TRIGS': [f'TRIG{i}' for i in range(1, 17)], 'KNOBS': list('ABCDEF'), 'ARROWS': ['UP', 'LEFT', 'DOWN', 'RIGHT'],
    'PAGES': ['SRC', 'AMP', 'LFO', 'FX1', 'FX2'], 'TRANSPORT': ['REC', 'PLAY', 'STOP'], 'RECS': ['REC1', 'REC2', 'REC3'],
    'SCENES': ['SCENEA', 'SCENEB'], 'TRACKS': [f'T{i}' for i in range(1, 9)],
}
KNOB_NAMES = {'LEVEL': 'Level', 'HPVOL': 'Headphones Vol'}


def expand(tok):
    """'TRIG1-8' -> TRIG1..TRIG8; 'T1-4'; a group name; or the control itself."""
    if tok in GROUPS:
        return GROUPS[tok]
    m = re.fullmatch(r'(TRIG|T)(\d+)-(\d+)', tok)
    if m:
        return [f'{m.group(1)}{i}' for i in range(int(m.group(2)), int(m.group(3)) + 1)]
    assert tok in C, 'no such control: ' + tok
    return [tok]


def parse(spec):
    """'FUNC MIDI LFO=MOD TRIG1-8=pads' -> ({control: True}, {control-or-group-anchor: tag})"""
    hi, tags = {}, []
    for tok in spec.split():
        name, _, tag = tok.partition('=')
        names = expand(name)
        for n in names:
            hi[n] = True
        if tag:
            tags.append((names, tag.replace('_', ' ')))
    return hi, tags


def _glyph(label, x, y, col, s=1.0):
    if label == '^':
        return f'<path d="M{x - 13 * s} {y + 8 * s} L{x} {y - 9 * s} L{x + 13 * s} {y + 8 * s}" fill="none" stroke="{col}" stroke-width="{4 * s}" stroke-linecap="round" stroke-linejoin="round"/>'
    if label == 'v':
        return f'<path d="M{x - 13 * s} {y - 8 * s} L{x} {y + 9 * s} L{x + 13 * s} {y - 8 * s}" fill="none" stroke="{col}" stroke-width="{4 * s}" stroke-linecap="round" stroke-linejoin="round"/>'
    if label == '<':
        return f'<path d="M{x + 8 * s} {y - 13 * s} L{x - 9 * s} {y} L{x + 8 * s} {y + 13 * s}" fill="none" stroke="{col}" stroke-width="{4 * s}" stroke-linecap="round" stroke-linejoin="round"/>'
    if label == '>':
        return f'<path d="M{x - 8 * s} {y - 13 * s} L{x + 9 * s} {y} L{x - 8 * s} {y + 13 * s}" fill="none" stroke="{col}" stroke-width="{4 * s}" stroke-linecap="round" stroke-linejoin="round"/>'
    if label == 'o':
        return f'<circle cx="{x}" cy="{y}" r="{12 * s}" fill="none" stroke="{col}" stroke-width="{4 * s}"/>'
    if label == '|>':
        return f'<path d="M{x - 9 * s} {y - 13 * s} L{x + 13 * s} {y} L{x - 9 * s} {y + 13 * s} Z" fill="none" stroke="{col}" stroke-width="{4 * s}" stroke-linejoin="round"/>'
    if label == '[]':
        return f'<rect x="{x - 12 * s}" y="{y - 12 * s}" width="{24 * s}" height="{24 * s}" rx="2" fill="none" stroke="{col}" stroke-width="{4 * s}"/>'
    if label == 'tempo':   # a metronome
        return (f'<path d="M{x - 9 * s} {y + 13 * s} L{x - 4 * s} {y - 13 * s} L{x + 4 * s} {y - 13 * s} L{x + 9 * s} {y + 13 * s} Z" fill="none" stroke="{col}" stroke-width="{3 * s}" stroke-linejoin="round"/>'
                f'<path d="M{x} {y + 6 * s} L{x + 11 * s} {y - 12 * s}" stroke="{col}" stroke-width="{3 * s}" stroke-linecap="round"/>')
    return None


def _control(name, state='normal', lamp=None):
    """state: normal | dim | hi.  lamp (night drawing): None or 0..3"""
    c = C[name]; x, y, w, h = c['x'], c['y'], c['w'], c['h']
    if lamp is not None:
        fill = ['#3b3e44', '#8a7f62', '#e3c56f', '#fff6cf'][lamp]; txt = ['#8d9096', '#fff', '#16181d', '#16181d'][lamp]
        ring = '' if lamp < 3 else f'<rect x="{x - w / 2 - 6}" y="{y - h / 2 - 6}" width="{w + 12}" height="{h + 12}" rx="13" fill="none" stroke="#fff6cf" stroke-width="4" opacity="0.55"/>'
    else:
        fill = {'normal': KEY, 'dim': DIM_KEY, 'hi': HI}[state]; txt = {'normal': KEY_TXT, 'dim': DIM_TXT, 'hi': HI_TXT}[state]; ring = ''
        if state == 'hi':
            ring = f'<rect x="{x - w / 2 - 5}" y="{y - h / 2 - 5}" width="{w + 10}" height="{h + 10}" rx="12" fill="none" stroke="{HI_RING}" stroke-width="5"/>'
    k = c['kind']; o = ''
    if k == 'key':
        o += ring + f'<rect x="{x - w / 2}" y="{y - h / 2}" width="{w}" height="{h}" rx="8" fill="{fill}"/>'
        lab = c['label']
        g = _glyph(lab, x, y, txt)
        if g:
            o += g
        elif name.startswith('TRIG'):
            o += f'<text x="{x}" y="{y + 11}" text-anchor="middle" font-size="32" font-weight="500" fill="{txt}">{lab}</text>'
            o += f'<rect x="{x - 13}" y="{y + 19}" width="26" height="4" fill="{txt}"/>'
        elif name in ('SCENEA', 'SCENEB'):
            o += f'<text x="{x}" y="{y + 12}" text-anchor="middle" font-size="34" font-weight="500" fill="{txt}">{lab}</text>'
        else:
            fs = 19 if len(lab) <= 4 else 17
            o += f'<text x="{x}" y="{y + fs * 0.36}" text-anchor="middle" font-size="{fs}" font-weight="700" fill="{txt}">{lab}</text>'
    elif k == 'knob':
        r = w / 2
        if lamp is not None:
            fill = '#3b3e44'
        if state == 'hi':
            o += f'<circle cx="{x}" cy="{y}" r="{r + 6}" fill="none" stroke="{HI_RING}" stroke-width="5"/>'
        o += f'<circle cx="{x}" cy="{y}" r="{r}" fill="{fill}"/>'
        o += f'<circle cx="{x}" cy="{y - r + 9}" r="3.2" fill="{txt}"/>'
        lab = KNOB_NAMES.get(name, name)
        col = INK if state == 'hi' else (theme.P_LEGEND if state == 'normal' else DIM_TXT)
        if lamp is not None:
            col = '#8d9096'
        o += f'<text x="{x}" y="{y + r + 21}" text-anchor="middle" font-size="{17 if len(lab) < 8 else 15}" font-weight="700" fill="{col}">{lab}</text>'
    elif k == 'fader':
        slot = DEEP if state != 'dim' else DIM_KEY
        tick = theme.P_LEGEND if state != 'dim' else DIM_KEY
        if lamp is not None:
            slot = '#3b3e44'; fill = '#55585e'; tick = '#3b3e44'
        for i in range(15):   # the scale's ticks
            tx = x - 112 + i * 16
            o += f'<path d="M{tx} {y - 48} V{y - 16} M{tx} {y + 16} V{y + 48}" stroke="{tick}" stroke-width="2" opacity="0.6"/>'
        o += f'<rect x="{x - 128}" y="{y - 6}" width="256" height="12" rx="6" fill="{slot}"/>'
        if state == 'hi':
            o += f'<rect x="{x - 142}" y="{y - 62}" width="284" height="124" rx="12" fill="none" stroke="{HI_RING}" stroke-width="5"/>'
        o += f'<rect x="{x - 22}" y="{y - 44}" width="44" height="88" rx="16" fill="{fill}"/>'
        o += f'<path d="M{x} {y - 36} V{y + 36}" stroke="{txt}" stroke-width="3"/>'
    elif k == 'led':
        col = '#fff6cf' if (lamp or 0) > 1 else fill
        o += f'<circle cx="{x}" cy="{y}" r="7" fill="{col}"/>'
        if state == 'hi':
            o += f'<circle cx="{x}" cy="{y}" r="14" fill="none" stroke="{HI_RING}" stroke-width="5"/>'
    elif k == 'screen':
        bez = {'normal': DEEP, 'dim': DIM_KEY, 'hi': HI}[state]; glass = {'normal': theme.P_GLASS, 'dim': theme.P_GLASS_DIM, 'hi': theme.P_GLASS}[state]
        if lamp is not None:
            bez, glass = '#3b3e44', '#16181d'
        o += f'<rect x="{x - w / 2}" y="{y - h / 2}" width="{w}" height="{h}" fill="{bez}"/>'
        o += f'<rect x="{x - 152}" y="{y - 106}" width="304" height="201" fill="{glass}"/>'
        if state == 'hi':
            o += f'<rect x="{x - w / 2 - 5}" y="{y - h / 2 - 5}" width="{w + 10}" height="{h + 10}" rx="6" fill="none" stroke="{HI_RING}" stroke-width="5"/>'
    return o


def _furniture(state='normal', night=False):
    """What is printed on the panel or always there: the top legends, the small LEDs, the group lines."""
    col = theme.P_LEGEND if state == 'normal' else theme.P_DIM_LEGEND
    led = theme.P_LED if state == 'normal' else DIM_KEY
    if night:
        col, led = '#6d7178', '#55585e'
    o = ''
    for x, t in [(168, 'Main Out'), (249, 'Cue Out'), (340, 'Input A B'), (421, 'Input C D'), (521, 'MIDI In'), (615, 'MIDI Out'), (708, 'MIDI Thru'), (919, 'Compact Flash'), (1121, 'USB'), (1309, 'DC In'), (1444, 'Power')]:
        o += f'<text x="{x}" y="26" text-anchor="middle" font-size="13" fill="{col}">{t}</text>'
    # a headphone sign over the headphone socket
    o += f'<path d="M72 28 v-6 a8 8 0 0 1 16 0 v6" fill="none" stroke="{col}" stroke-width="2"/><rect x="70" y="24" width="4" height="7" fill="{col}"/><rect x="86" y="24" width="4" height="7" fill="{col}"/>'
    for i, x in enumerate([201, 238, 276, 313, 350, 388]):   # the six input LEDs over the recorder keys
        o += f'<circle cx="{x}" cy="175" r="7" fill="{led}"/>'
    for x, t in [(201, 'A'), (238, 'B'), (276, 'C'), (313, 'D'), (369, 'Int')]:
        o += f'<text x="{x}" y="158" text-anchor="middle" font-size="12" fill="{col}">{t}</text>'
    o += f'<circle cx="369" cy="295" r="6" fill="{led}"/>'                      # the small LED over ARR
    o += f'<text x="1005" y="84" text-anchor="middle" font-size="12" fill="{col}">Card Status</text>'
    for x in (1126, 1430):                                                       # the two scene LEDs
        o += f'<circle cx="{x}" cy="543" r="7" fill="{led}"/>'
    for i, x in enumerate([1464, 1490, 1515, 1541]):                             # the four page LEDs over PAGE
        o += f'<circle cx="{x}" cy="650" r="6" fill="{led}"/><text x="{x}" y="636" text-anchor="middle" font-size="11" fill="{col}">{i + 1}:4</text>'
    return o


def _tag(names, text, taken=None):
    """A small label on a control, or over a run of controls, saying what it is in this context. `taken`: the
    rectangles of the tags already placed; a tag that would cover one of them goes under its key instead."""
    xs = [C[n]['x'] for n in names]; c0 = C[names[0]]
    x0, x1 = min(xs) - c0['w'] / 2, max(xs) + c0['w'] / 2
    same_row = all(abs(C[n]['y'] - c0['y']) < 2 for n in names)
    w = max(sum(2 if unicodedata.east_asian_width(ch) in 'WF' else 1 for ch in text) * 11.4 + 16, 40)   # a CJK letter is two wide
    taken = taken if taken is not None else []

    def draw(cx, y):
        taken.append((cx - w / 2, y, cx + w / 2, y + 26))
        return f'<rect x="{cx - w / 2}" y="{y}" width="{w}" height="26" rx="6" fill="{HI_RING}"/><text x="{cx}" y="{y + 19}" text-anchor="middle" font-size="19" font-weight="700" fill="{theme.ON_ACC}">{text}</text>'

    def clash(cx, y):
        return any(cx - w / 2 < r[2] + 2 and cx + w / 2 > r[0] - 2 and y < r[3] and y + 26 > r[1] for r in taken)
    if len(names) > 1 and same_row:
        cx = (x0 + x1) / 2; y = c0['y'] - c0['h'] / 2 - 24
        return f'<path d="M{x0 + 4} {y + 13} H{x1 - 4}" stroke="{HI_RING}" stroke-width="4"/>' + draw(cx, y)
    o = ''
    for n in names:
        c = C[n]
        y = c['y'] + c['h'] / 2 + 30 if c['kind'] == 'knob' else c['y'] - c['h'] / 2 - 21
        if c['kind'] == 'fader':
            y = c['y'] - 94
        cx = min(max(c['x'], w / 2 + 6), W - w / 2 - 6)
        if clash(cx, y) and c['kind'] == 'key':
            y = c['y'] + c['h'] / 2 - 5
        o += draw(cx, y)
    return o


def _svg(inner, vb, cls, label):
    return f'<svg class="{cls}" viewBox="{vb}" xmlns="http://www.w3.org/2000/svg" {FONT} role="img" aria-label="{label}">{inner}</svg>'


def mini(spec='', cls='panel mini'):
    """The panel with only the controls of `spec` lit; NAME=tag writes what the control is called here."""
    hi, tags = parse(spec)
    o = f'<rect x="0" y="0" width="{W}" height="{H}" rx="10" fill="{DIM_PANEL}" stroke="{theme.P_DIM_EDGE}" stroke-width="2"/>' + _furniture('dim')
    for n in C:
        if n not in hi:
            o += _control(n, 'dim')
    for n in C:
        if n in hi:
            o += _control(n, 'hi')
    taken = []
    for names, t in tags:
        o += _tag(names, t, taken)
    return _svg(o, f'-6 -6 {W + 12} {H + 12}', cls, 'The panel, with the controls of this section marked')


def lamps(levels, tags='', cls='panel mini'):
    """The panel in the dark. levels: 'TRIG1=3 TRIG5=2 T3=3 PLAY=3 ...' (0 dark, 1 dim, 2 lit, 3 full)."""
    lv = {}
    for tok in levels.split():
        name, _, v = tok.partition('=')
        for n in expand(name):
            lv[n] = int(v)
    o = f'<rect x="0" y="0" width="{W}" height="{H}" rx="10" fill="{theme.P_NIGHT}" stroke="{theme.P_DIM_EDGE}" stroke-width="2"/>' + _furniture('dim', night=True)
    for n in C:
        o += _control(n, 'normal', lamp=lv.get(n, 0))
    if tags:
        for names, t in parse(tags)[1]:
            o += _tag(names, t)
    return _svg(o, f'-6 -6 {W + 12} {H + 12}', cls, 'The lamps of the panel')


def trig_row(levels, tags=None, cls='trigrow'):
    """Sixteen trigs in a row, each lamp 0..3; tags: {index (1-based): text} written under a trig."""
    o = f'<rect x="1" y="1" width="1458" height="148" rx="10" fill="{theme.P_NIGHT}" stroke="{theme.P_DIM_EDGE}" stroke-width="2"/>'
    for i in range(16):
        l = int(levels[i]); x = 50 + i * 88.5 + (i // 4) * 8
        fill = ['#3b3e44', '#8a7f62', '#e3c56f', '#fff6cf'][l]; txt = ['#8d9096', '#fff', '#16181d', '#16181d'][l]
        if l == 3:
            o += f'<rect x="{x - 46}" y="9" width="92" height="92" rx="13" fill="none" stroke="#fff6cf" stroke-width="4" opacity="0.55"/>'
        o += f'<rect x="{x - 40}" y="15" width="80" height="80" rx="8" fill="{fill}"/><text x="{x}" y="66" text-anchor="middle" font-size="32" font-weight="500" fill="{txt}">{i + 1}</text>'
        if tags and (i + 1) in tags:
            o += f'<text x="{x}" y="130" text-anchor="middle" font-size="21" font-weight="600" fill="#e9eaec">{tags[i + 1]}</text>'
    return _svg(o, '0 0 1460 150', cls, 'The sixteen trig lamps')


def _badge(x, y, n, r=25):
    fs = 27 if n < 10 else 24
    return f'<circle cx="{x}" cy="{y}" r="{r}" fill="{INK}" stroke="{theme.BG}" stroke-width="3"/><text x="{x}" y="{y + fs * 0.36}" text-anchor="middle" font-size="{fs}" font-weight="700" fill="{ON_INK}">{n}</text>'


def _box(names, pad=9):
    xs0 = min(C[n]['x'] - C[n]['w'] / 2 for n in names) - pad; xs1 = max(C[n]['x'] + C[n]['w'] / 2 for n in names) + pad
    ys0 = min(C[n]['y'] - C[n]['h'] / 2 for n in names) - pad; ys1 = max(C[n]['y'] + C[n]['h'] / 2 + (22 if C[n]['kind'] == 'knob' else 0) for n in names) + pad
    return xs0, ys0, xs1, ys1


# the callouts of the front panel: number, the controls it frames, where its badge sits, and the leader's path
FRONT_CALLOUTS = [
    (1, ['REC1', 'REC2'], 'T', 257), (2, ['REC3'], 'T', 369), (3, ['T1', 'T2', 'T3', 'T4'], 'T', 514), (4, ['SCREEN'], 'T', 750),
    (5, ['CARD'], 'T', 1005), (6, ['LEVEL'], 'T', 1135), (7, ['A', 'B', 'C', 'D', 'E', 'F'], 'T', 1387),
    (8, ['TEMPO'], 'R', 400), (9, ['SCENEA', 'SCENEB'], 'R', 543), (10, ['PAGE'], 'R', 701),
    (11, ['XFADER'], 'B', 1278), (12, ['STOP'], 'B', 882), (13, ['PLAY'], 'B', 794), (14, ['REC'], 'B', 705),
    (15, ['SRC', 'AMP', 'LFO', 'FX1', 'FX2'], 'B', 620), (16, ['UP', 'LEFT', 'DOWN', 'RIGHT'], 'B', 486),
    (17, ['NO'], 'B', 356), (18, ['YES'], 'B', 300), (19, ['BANK'], 'B', 228), (20, ['CUE'], 'B', 163),
    (21, [f'TRIG{i}' for i in range(1, 17)], 'L', 698), (22, ['PTN'], 'L', 580), (23, ['FUNC'], 'L', 468),
    (24, ['PROJ'], 'K', 0), (25, ['PART'], 'K', 0), (26, ['AED'], 'K', 0), (27, ['MIX'], 'K', 0), (28, ['ARR'], 'K', 0),
    (29, ['MIDI'], 'L', 254), (30, ['HPVOL'], 'L', 151),
]


def front():
    """The whole panel, every control framed and numbered."""
    o = f'<rect x="0" y="0" width="{W}" height="{H}" rx="10" fill="{PANEL}" stroke="{PANEL_EDGE}" stroke-width="2.5"/>' + _furniture('normal')
    for n in C:
        o += _control(n, 'normal')
    line = f'fill="none" stroke="{CALL}" stroke-width="3.2" stroke-linejoin="round"'
    TOP, BOT, LEFT, RIGHT = -92, H + 96, -112, W + 112
    for num, names, side, at in FRONT_CALLOUTS:
        x0, y0, x1, y1 = _box(names, 8 if names[0] != 'HPVOL' else 4)
        if names == ['SCREEN']:
            cx, cy = C['SCREEN']['x'], C['SCREEN']['y']
            o += f'<path d="M{at} {TOP} V{cy}" {line}/><circle cx="{cx}" cy="{cy}" r="13" fill="{INK}"/>' + _badge(at, TOP, num)
            continue
        if num == 3:             # the track keys: both columns, joined under the screen
            b5 = _box(['T5', 'T6', 'T7', 'T8'], 8)
            o += f'<rect x="{b5[0]}" y="{b5[1]}" width="{b5[2] - b5[0]}" height="{b5[3] - b5[1]}" {line}/><path d="M{x1} 403 H{b5[0]}" {line}/>'
        if side == 'K':          # the badge sits on the key's lower edge
            o += f'<rect x="{x0}" y="{y0}" width="{x1 - x0}" height="{y1 - y0}" {line}/>' + _badge(C[names[0]]['x'], y1 + 1, num, r=21)
            continue
        if names[0] in ('REC', 'PLAY', 'STOP'):   # a dot on the key, as on the scene and screen
            c = C[names[0]]
            o += f'<path d="M{at} {BOT} V{c["y"]}" {line}/><circle cx="{c["x"]}" cy="{c["y"] + 0}" r="0" fill="{INK}"/>' + _badge(at, BOT, num)
            o += f'<rect x="{x0}" y="{y0}" width="{x1 - x0}" height="{y1 - y0}" {line}/>'
            continue
        if names == ['SCENEA', 'SCENEB']:
            a, b = _box(['SCENEA']), _box(['SCENEB'])
            o += f'<rect x="{a[0]}" y="{a[1]}" width="{a[2] - a[0]}" height="{a[3] - a[1]}" {line}/><rect x="{b[0]}" y="{b[1]}" width="{b[2] - b[0]}" height="{b[3] - b[1]}" {line}/>'
            o += f'<path d="M{C["SCENEA"]["x"]} {a[1]} V{a[1] - 26} H{C["SCENEB"]["x"]} V{b[1]}" {line}/><path d="M{b[2]} {at} H{RIGHT}" {line}/>' + _badge(RIGHT, at, num)
            continue
        if names == ['XFADER']:
            c = C['XFADER']
            o += f'<rect x="{c["x"] - 34}" y="{c["y"] - 56}" width="68" height="112" {line}/><path d="M{at} {c["y"] + 56} V{BOT}" {line}/>' + _badge(at, BOT, num)
            continue
        if names == ['TEMPO']:
            o += f'<rect x="{x0}" y="{y0}" width="{x1 - x0}" height="{y1 - y0}" {line}/><path d="M{C["TEMPO"]["x"]} {y1} V{at} H{RIGHT}" {line}/>' + _badge(RIGHT, at, num)
            continue
        if num == 21:            # the trigs: a bracket at the left
            o += f'<path d="M{x0 + 14} {y0} H{x0} V{y1} H{x0 + 14}" {line}/><path d="M{x0} {at} H{LEFT}" {line}/>' + _badge(LEFT, at, num)
            continue
        if num == 15:            # the page keys: a bracket underneath
            o += f'<path d="M{x0} {y1 - 14} V{y1} H{x1} V{y1 - 14}" {line}/><path d="M{at} {y1} V{BOT}" {line}/>' + _badge(at, BOT, num)
            continue
        if num == 16:            # the arrows: a bracket under the lower three
            a = _box(['LEFT', 'DOWN', 'RIGHT'])
            o += f'<path d="M{a[0]} {a[3] - 14} V{a[3]} H{a[2]} V{a[3] - 14}" {line}/><path d="M{at} {a[3]} V{BOT}" {line}/>' + _badge(at, BOT, num)
            continue
        o += f'<rect x="{x0}" y="{y0}" width="{x1 - x0}" height="{y1 - y0}" {line}/>'
        if side == 'T':
            o += f'<path d="M{at} {y0} V{TOP}" {line}/>' + _badge(at, TOP, num)
        elif side == 'B':
            if num in (18, 20):  # YES and CUE sit above NO and BANK: the leader leaves by the key's left edge
                o += f'<path d="M{x0} {(y0 + y1) / 2} H{at} V{BOT}" {line}/>' + _badge(at, BOT, num)
            else:
                o += f'<path d="M{at} {y1} V{BOT}" {line}/>' + _badge(at, BOT, num)
        elif side == 'L':
            o += f'<path d="M{x0} {at} H{LEFT}" {line}/>' + _badge(LEFT, at, num)
        else:
            o += f'<path d="M{x1} {at} H{RIGHT}" {line}/>' + _badge(RIGHT, at, num)
    return _svg(o, f'{LEFT - 34} {TOP - 34} {RIGHT - LEFT + 68} {BOT - TOP + 68}', 'panel front', 'Octatrack MKII front panel, numbered')


def rear():
    """The back of the unit, seen from behind, every connector numbered."""
    RW, RH = 1585, 250
    o = f'<rect x="0" y="0" width="{RW}" height="{RH}" rx="8" fill="{PANEL}" stroke="{PANEL_EDGE}" stroke-width="2.5"/>'
    txt = f'font-size="17" fill="{theme.P_REAR_TXT}" text-anchor="middle"'
    line = f'fill="none" stroke="{CALL}" stroke-width="3.2"'

    def jack(x, y, r=31):
        return f'<circle cx="{x}" cy="{y}" r="{r}" fill="{theme.P_REAR_METAL}"/><circle cx="{x}" cy="{y}" r="{r - 9}" fill="{theme.P_REAR_MID}"/><circle cx="{x}" cy="{y}" r="{r - 20}" fill="#0d0e11"/>'

    def din(x, y):
        s = f'<circle cx="{x}" cy="{y}" r="35" fill="{theme.P_REAR_METAL}"/><circle cx="{x}" cy="{y}" r="26" fill="{theme.P_REAR_HOLE}"/>'
        for dx, dy in [(-15, 6), (-9, 15), (0, 19), (9, 15), (15, 6)]:
            s += f'<circle cx="{x + dx}" cy="{y + dy}" r="2.6" fill="#c9cbce"/>'
        return s
    # 1 power switch, 2 DC in, 3 USB, 4 card slot
    o += f'<rect x="106" y="124" width="100" height="72" rx="4" fill="{theme.P_REAR_METAL}"/><rect x="118" y="134" width="76" height="52" rx="3" fill="{theme.P_REAR_HOLE}"/><circle cx="140" cy="160" r="6" fill="none" stroke="#fff" stroke-width="2.4"/><path d="M172 152 V168" stroke="#fff" stroke-width="2.4"/>'
    o += f'<text x="156" y="222" {txt}>Power</text>'
    o += f'<rect x="270" y="138" width="42" height="52" rx="3" fill="{theme.P_REAR_METAL}"/><circle cx="291" cy="164" r="12" fill="#0d0e11"/><circle cx="291" cy="164" r="3.5" fill="#c9cbce"/>'
    o += f'<text x="291" y="222" {txt}>12VDC 2A</text>'
    o += f'<rect x="446" y="140" width="58" height="52" rx="3" fill="{theme.P_REAR_METAL}"/><path d="M455 184 V154 L462 147 H488 L495 154 V184 Z" fill="#0d0e11"/>'
    o += f'<text x="475" y="222" {txt}>USB</text>'
    o += f'<rect x="556" y="168" width="232" height="22" rx="2" fill="#0d0e11"/><rect x="766" y="172" width="16" height="14" fill="{theme.P_REAR_METAL}"/>'
    o += f'<text x="672" y="222" {txt}>Compact Flash</text>'
    for i, n in enumerate(['MIDI Thru', 'MIDI Out', 'MIDI In']):
        o += din(874 + i * 90, 150) + f'<text x="{874 + i * 90}" y="222" {txt}>{n}</text>'
    # the jacks: inputs C A over D B; cue L over R; main L over R; headphones
    for x, top, bot in [(1155, 'C', 'D'), (1235, 'A', 'B'), (1321, 'L', 'R'), (1401, 'L', 'R')]:
        o += jack(x, 84) + jack(x, 164)
        o += f'<text x="{x + 41}" y="62" font-size="15" fill="{theme.P_REAR_TXT}" text-anchor="middle">{top}</text><text x="{x + 41}" y="142" font-size="15" fill="{theme.P_REAR_TXT}" text-anchor="middle">{bot}</text>'
    o += f'<text x="1195" y="222" {txt}>Inputs</text><text x="1321" y="222" {txt}>Cue Out</text><text x="1401" y="222" {txt}>Main Out</text>'
    o += f'<path d="M1125 216 H1163 M1227 216 H1265" stroke="{theme.P_REAR_TXT}" stroke-width="1.6"/>'
    o += jack(1488, 170, 30) + f'<text x="1492" y="222" {txt}>Headphones</text>'
    # the callouts, along the top
    TOP = -70
    for num, x, y in [(1, 156, 124), (2, 291, 138), (3, 475, 140), (4, 672, 168), (5, 874, 115), (6, 964, 115), (7, 1054, 115), (8, 1195, 40), (9, 1321, 50), (10, 1401, 50), (11, 1488, 138)]:
        o += f'<path d="M{x} {TOP} V{y}" {line}/>'
        if num == 8:
            o += f'<path d="M1120 40 H1270" {line}/>'
        o += _badge(x, TOP, num)
    return _svg(o, f'-20 {TOP - 34} {RW + 40} {RH - TOP + 48}', 'panel rear', 'Octatrack MKII rear panel, numbered')


def export(out_dir, chrome=None):
    """The two numbered drawings and a sample of the others as stand-alone files: panel_*.svg, and panel_*.png when
    a Chrome is given to draw them (on the theme's own ground, 2x). The dark theme's files are panel_<name>.*, the
    light theme's panel_<name>_light.*, so the two never overwrite each other."""
    import os, subprocess
    pages = {'front': (front(), 1881, 1130), 'rear': (rear(), 1625, 402),
             'mini_example': (mini('FUNC MIDI LFO=MOD FX1=FLT FX2=FX ARR=GEN PAGE=PTN TRIG1-8=pads TRIG9-16=mutes KNOBS LEVEL=volume'), 1597, 832),
             'lamps_example': (lamps('TRIG1=2 TRIG5=1 TRIG7=2 TRIG9=2 TRIG11=3 TRIG13=1 T3=3 T1=2 T6=0 T2=1 T4=1 T5=1 T7=1 T8=1 PLAY=3 STOP=1 REC=1 SRC=3 AMP=1 LFO=1 FX1=1 FX2=1 ARR=1 MIDI=1 TEMPO=3'), 1597, 832)}
    made = []
    for k, (svg, w, h) in pages.items():
        k = k if theme.NAME == 'dark' else f'{k}_{theme.NAME}'
        f = os.path.join(out_dir, f'panel_{k}.svg')
        open(f, 'w').write('<?xml version="1.0" encoding="UTF-8"?>\n' + svg.replace('<svg class="', f'<svg width="{w}" height="{h}" class="', 1))
        made.append(f)
        if chrome:
            html = os.path.join(out_dir, f'_panel_{k}.html')
            open(html, 'w').write(f'<body style="margin:0;background:{theme.BG}"><div style="width:{w}px">{svg}</div></body>')
            png = os.path.join(out_dir, f'panel_{k}.png')
            subprocess.run([chrome, '--headless', '--disable-gpu', '--hide-scrollbars', '--force-device-scale-factor=2', f'--window-size={w},{h}', f'--screenshot={png}', 'file://' + html],
                           check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            os.remove(html); made.append(png)
    return made


if __name__ == '__main__':
    import sys
    out = sys.argv[1] if len(sys.argv) > 1 else '.'
    for f in export(out, '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'):
        print(f)
