"""The AERIA OS manual's block diagrams, as inline SVG: the signal's way through the multi-effect (the sky) and through
the sequencer, the output section, what is kept where, the map of pages. Our own drawings."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import theme, lang
# the colours of the theme in use (theme.py: reload this module after theme.use(); build_manual.set_theme does). A
# "plain" box is the page's own ground with an outline; a "dark" box is the one that stands out: black with white
# lettering on the light pages, a lighter slab with bright lettering on the dark ones.
# Every word drawn goes through lang.tr(): the English here is the key of the translations in lang.py (DIAGRAMS).
INK, SOFT, ACC, BLUE, TINT = theme.INK, theme.SOFT, theme.ACC, theme.BLUE, theme.TINT
BG, BRIGHT, FAINT = theme.BG, theme.BRIGHT, theme.FAINT
SLAB, SLAB_EDGE, SLAB_TXT = theme.D_SLAB, theme.D_SLAB_EDGE, theme.D_SLAB_TXT
SLAB_SOFT = theme.INK if theme.NAME == 'dark' else theme.RULE      # quieter lettering on a slab
SLAB_ACC = theme.ACC if theme.NAME == 'dark' else theme.THEMES['dark']['ACC']   # the accent on a slab
tr = lang.tr
FONT = 'font-family="Helvetica Neue, Helvetica, Arial, sans-serif"'


class D:
    def __init__(self, w, h):
        self.w, self.h, self.o = w, h, []
        self.o.append(f'<defs><marker id="ah" viewBox="0 0 10 10" refX="8.5" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="{INK}"/></marker>'
                      f'<marker id="aha" viewBox="0 0 10 10" refX="8.5" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="{ACC}"/></marker>'
                      f'<marker id="ahb" viewBox="0 0 10 10" refX="8.5" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="{BLUE}"/></marker></defs>')

    def box(self, x, y, w, h, title, sub=None, kind='plain', fs=15, sfs=13):
        title, sub = tr(title), (tr(sub) if sub else sub)
        fill, stroke, txt = {'plain': (BG, INK, INK), 'dark': (SLAB, SLAB_EDGE, SLAB_TXT), 'tint': (theme.D_TINT_BOX, FAINT, INK), 'acc': (theme.ACC_GROUND, ACC, INK), 'blue': (theme.BLUE_GROUND, BLUE, INK), 'ghost': (BG, FAINT, SOFT)}[kind]
        dash = ' stroke-dasharray="5 4"' if kind == 'ghost' else ''
        self.o.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="6" fill="{fill}" stroke="{stroke}" stroke-width="1.8"{dash}/>')
        lines = title.split('|'); subs = sub.split('|') if sub else []
        total = len(lines) * (fs + 3) + len(subs) * (sfs + 2.5)
        ty = y + (h - total) / 2 + fs
        for l in lines:
            self.o.append(f'<text x="{x + w / 2}" y="{ty}" text-anchor="middle" font-size="{fs}" font-weight="700" fill="{txt}">{l}</text>'); ty += fs + 3
        ty -= 2
        for l in subs:
            self.o.append(f'<text x="{x + w / 2}" y="{ty}" text-anchor="middle" font-size="{sfs}" fill="{txt if kind == "dark" else SOFT}">{l}</text>'); ty += sfs + 2.5

    def arrow(self, pts, kind='', label=None, lpos=None, dash=False):
        col = {'': INK, 'a': ACC, 'b': BLUE}[kind]
        d = 'M' + ' L'.join(f'{x} {y}' for x, y in pts)
        da = ' stroke-dasharray="5 4"' if dash else ''
        self.o.append(f'<path d="{d}" fill="none" stroke="{col}" stroke-width="2" marker-end="url(#ah{kind})"{da}/>')
        if label:
            x, y = lpos if lpos else ((pts[0][0] + pts[-1][0]) / 2, (pts[0][1] + pts[-1][1]) / 2 - 6)
            self.text(x, y, label, 12, col if kind else SOFT)

    def line(self, pts, col=INK, dash=False):
        d = 'M' + ' L'.join(f'{x} {y}' for x, y in pts)
        da = ' stroke-dasharray="5 4"' if dash else ''
        self.o.append(f'<path d="{d}" fill="none" stroke="{col}" stroke-width="2"{da}/>')

    def text(self, x, y, t, fs=12, col=SOFT, anchor='middle', bold=False):
        for i, l in enumerate(tr(t).split('|')):
            fw = ' font-weight="700"' if bold else ''
            self.o.append(f'<text x="{x}" y="{y + i * (fs + 2.5)}" text-anchor="{anchor}" font-size="{fs}" fill="{col}"{fw}>{l}</text>')

    def band(self, x, y, w, h, title, col=TINT):
        title = tr(title)
        ls = ' letter-spacing="1.2"' if title.isascii() else ''     # no spacing out of CJK lettering
        self.o.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="8" fill="{col}"/>')
        self.o.append(f'<text x="{x + 10}" y="{y + 17}" font-size="11.5" font-weight="700" fill="{SOFT}"{ls}>{title}</text>')

    def svg(self, label):
        return f'<svg class="diagram" viewBox="0 0 {self.w} {self.h}" xmlns="http://www.w3.org/2000/svg" {FONT} role="img" aria-label="{tr(label)}">{"".join(self.o)}</svg>'


def flow_sky():
    d = D(1000, 520)
    d.band(8, 8, 190, 330, 'WHAT GOES IN')
    d.band(322, 8, 444, 330, 'THE CHAIN: UP TO SEVEN EFFECTS, PLACED ON THE TWO DSP CORES')
    d.band(8, 352, 984, 160, 'THE MASTER SECTION AND THE OUTPUTS (THE WEATHER NEVER MOVES IT)')
    d.box(22, 36, 162, 46, 'Inputs A B', 'gain AB, stereo or mono')
    d.box(22, 92, 162, 46, 'Inputs C D', 'gain CD, stereo or mono')
    d.box(22, 156, 162, 40, 'LOOP', 'one looping sample', 'tint')
    d.box(22, 204, 162, 40, 'STARS', '16 one-shots', 'tint')
    d.box(22, 252, 162, 40, 'BALLOONS', '3 recordings, long grains', 'tint')
    d.text(103, 314, 'each source has its own SEND', 12)
    d.box(226, 130, 76, 60, 'DRIV', 'GAIN page')
    for y in (59, 115, 176, 224, 272):
        d.line([(184, y), (206, y)])
    d.line([(206, 59), (206, 272)])
    d.arrow([(206, 160), (226, 160)])
    for i in range(7):
        x = 336 + i * 60
        d.box(x, 134, 50, 52, str(i + 1), 'slot', 'dark', fs=17)
        if i:
            d.arrow([(x - 10, 160), (x, 160)])
    d.arrow([(302, 160), (336, 160)])
    d.text(544, 50, 'Each slot holds one of the sixteen effects, or is empty.|The weather moves the effects&#8217; knobs; scenes and the crossfader too.', 12.5)
    d.arrow([(421, 186), (421, 232), (560, 232)], 'a')
    d.box(560, 212, 190, 40, 'OUT: MAIN, CUE or QUAD', 'a slot taken out as a branch', 'acc', fs=13)
    d.arrow([(760, 134), (760, 106), (319, 106), (319, 157)], 'b')
    d.text(545, 100, 'main feedback: FDBK, FTON, WARM (MIX page)', 12, BLUE)
    d.arrow([(746, 160), (790, 160)])
    d.box(790, 126, 96, 68, 'Weather|filters', 'rain, thunder,|lightning, HELI', 'blue', fs=13.5)
    d.arrow([(838, 194), (838, 300), (745, 300), (745, 384)])
    d.box(690, 384, 110, 56, 'Freeze|delay', 'ping-pong, WET', 'plain', fs=13.5)
    d.box(566, 384, 104, 56, 'OUT, BAL', 'level, balance', fs=13.5)
    d.box(452, 384, 94, 56, '4-band|EQ', None, fs=13.5)
    d.box(338, 384, 94, 56, 'Bass|mono', 'under 50 Hz', fs=13.5)
    d.box(224, 384, 94, 56, 'Limiter', '-1 dBFS', fs=13.5)
    for a, b in ((690, 670), (566, 546), (452, 432), (338, 318)):
        d.arrow([(a, 412), (b, 412)])
    d.arrow([(224, 400), (150, 400)])
    d.box(22, 382, 128, 34, 'MAIN OUT L R', None, 'dark', fs=13)
    d.box(22, 422, 128, 30, 'USB 5-6', None, 'tint', fs=13)
    d.box(22, 468, 128, 34, 'CUE OUT L R', None, 'dark', fs=13)
    d.line([(186, 400), (186, 437)])
    d.arrow([(186, 437), (150, 437)])
    d.arrow([(750, 232), (984, 232), (984, 485), (150, 485)], 'a')
    d.text(520, 478, 'slots routed to CUE or panned toward it (with nothing routed there: a copy of MAIN)', 12, ACC)
    d.box(836, 384, 130, 50, 'HEADPHONES', 'MAIN + CUE', 'dark', fs=13)
    d.text(103, 148, '', 11)
    d.text(900, 456, 'USB 1-4: the four inputs', 12)
    return d.svg('The signal through the multi-effect')


def flow_seq():
    d = D(1000, 580)
    d.band(8, 8, 330, 320, 'MAIN PROCESSOR: EACH TRACK')
    d.band(352, 8, 640, 320, 'DSP: A CHANNEL FOR EACH TRACK')
    d.band(8, 342, 984, 230, 'THE MASTER SECTION, SHARED WITH THE MULTI-EFFECT')
    d.box(20, 36, 150, 132, 'Machine', 'SMPL  sample|SUB  sine|FM  two sines|NOIS  noise|PLUK  string|MIDI  no sound', 'dark', fs=14)
    d.box(190, 110, 138, 46, 'VOL, ducking', 'level, DUCK', fs=13)
    d.arrow([(170, 63), (259, 63), (259, 110)])
    d.text(95, 190, 'each step: velocity, locks,|ratchet, chance, condition', 12)
    d.box(20, 228, 150, 44, 'MIDI OUT', 'note or chord, GATE', 'tint', fs=13)
    d.arrow([(95, 212), (95, 228)], dash=True)
    d.arrow([(240, 156), (240, 300), (93, 300), (93, 376)], 'a')
    d.text(250, 292, 'DLY send (after the level)', 12, ACC, 'start')
    d.box(370, 110, 78, 46, 'HP', '12 dB/oct', fs=14)
    d.box(462, 110, 78, 46, 'LP', '12 dB/oct', fs=14)
    d.box(554, 98, 120, 70, 'One effect', 'any of the sixteen,|or none', 'dark', fs=14)
    d.box(760, 110, 96, 46, 'PAN', 'equal power', fs=14)
    d.box(876, 110, 104, 46, 'OUT', 'MAIN CUE BOTH QUAD', fs=12)
    d.arrow([(328, 133), (370, 133)]); d.arrow([(448, 133), (462, 133)]); d.arrow([(540, 133), (554, 133)]); d.arrow([(674, 133), (760, 133)]); d.arrow([(856, 133), (876, 133)])
    d.arrow([(716, 133), (716, 212)], 'b')
    d.box(616, 212, 200, 50, 'STEM to USB', 'after the effect, before the pan', 'blue', fs=13.5)
    d.text(716, 284, 'tracks 1-8 on USB channels 3-18 (the 18-channel format)', 12, BLUE)
    d.text(455, 62, 'filters always there,|resonance fixed at 20 %', 12)
    d.text(928, 62, 'x 8 tracks,|summed', 12)
    d.box(24, 376, 138, 56, 'Freeze delay', 'fed by the DLY sends', fs=13.5)
    d.box(600, 376, 110, 56, 'OUT, BAL', None, fs=13.5)
    d.box(476, 376, 104, 56, '4-band EQ', None, fs=13.5)
    d.box(352, 376, 104, 56, 'Bass mono', 'under 50 Hz', fs=13.5)
    d.box(228, 476, 104, 56, 'Limiter', '-1 dBFS', fs=13.5)
    d.arrow([(912, 156), (912, 404), (710, 404)])
    d.text(815, 396, 'the eight tracks, summed', 12)
    d.arrow([(600, 404), (580, 404)]); d.arrow([(476, 404), (456, 404)])
    d.arrow([(352, 404), (280, 404), (280, 476)])
    d.arrow([(162, 396), (200, 396), (200, 360), (655, 360), (655, 376)], 'a')
    d.box(24, 476, 138, 34, 'MAIN OUT L R', None, 'dark', fs=13)
    d.box(24, 518, 138, 34, 'USB: main pair', None, 'tint', fs=13)
    d.arrow([(228, 493), (162, 493)])
    d.line([(195, 493), (195, 535)]); d.arrow([(195, 535), (162, 535)])
    d.box(420, 482, 150, 46, 'Inputs A B C D', 'pass dry to the output', 'ghost', fs=13)
    d.arrow([(570, 505), (655, 505), (655, 432)], dash=True)
    d.box(752, 470, 228, 62, 'CUE OUT L R', 'tracks with OUT at CUE or BOTH|(not through the master section)', 'dark', fs=13)
    d.arrow([(956, 156), (956, 470)], 'a')
    return d.svg('The signal through the sequencer')


def memory():
    d = D(1000, 372)
    d.band(8, 8, 600, 356, 'BATTERY-BACKED MEMORY: KEPT WHILE THE POWER IS OFF')
    d.band(622, 8, 370, 196, 'THE COMPACTFLASH CARD')
    d.band(622, 218, 370, 146, 'NOT KEPT')
    d.box(24, 40, 180, 84, 'The saved state', 'every knob, the chain,|weathers, master, scale|written 2 s after a change', 'dark', fs=14)
    d.box(216, 40, 180, 84, '16 scenes', 'and which two the|crossfader holds|written 2 s after a change', 'dark', fs=14)
    d.box(408, 40, 186, 84, 'The sequence', '4 patterns, 8 tracks,|effects, mutes, chain|kept at every touch', 'dark', fs=14)
    d.box(24, 142, 570, 40, 'Which samples are loaded', 'the loop, each bank&#8217;s folder, the kit, PONG&#8217;s folder: names only', 'tint', fs=13.5)
    d.box(24, 232, 570, 116, '16 projects, each with a name', 'a project is a copy of all four of the above|PROJ, a trig, FUNC+YES saves  &#183;  YES loads', 'acc', fs=15)
    d.arrow([(114, 182), (114, 232)], 'a'); d.arrow([(306, 182), (306, 232)], 'a'); d.arrow([(500, 182), (500, 232)], 'a')
    d.text(208, 212, 'save copies down,', 12, ACC); d.text(404, 212, 'load copies back up', 12, ACC)
    d.box(638, 40, 338, 50, 'Your WAV files', 'loops, folders of stars, kits, PONG folders', 'plain', fs=14)
    d.text(807, 112, 'AERIA OS only reads the card. A project keeps the|places of its samples, not their sound: the card|must hold the same files in the same folders.', 12)
    d.arrow([(638, 172), (594, 162)], 'b')
    d.text(646, 178, 'read again at start-up and when a project loads', 12, BLUE, 'start')
    d.text(640, 250, 'The balloons&#8217; recordings (sample memory)|Whether the delay is frozen|Whether the sequencer was playing|The settings of GUMBALLS and PITCH|What was copied with FUNC + REC', 13, INK, 'start')
    return d.svg('What is kept where')


def faces():
    d = D(1000, 250)
    d.box(40, 30, 380, 190, 'THE SKY', 'the multi-effect|inputs and sources through a chain of|up to seven effects, moved by the weather||pages: CHAIN SKY SOURCE LOOP STARS BALLOON|MIX GAIN MASTER OUT CLOUD MIDI SYSTEM', 'plain', fs=22)
    d.box(580, 30, 380, 190, 'THE SEQUENCER', 'the groove box|eight tracks on the sixteen trigs,|each with filters and one effect||pages: SRC AMP MOD FLT FX GEN MIDI PTN|the mixer, MASTER', 'dark', fs=22)
    d.arrow([(420, 100), (580, 100)]); d.arrow([(580, 150), (420, 150)])
    d.o.append(f'<rect x="446" y="106" width="108" height="38" rx="6" fill="{BG}" stroke="{ACC}" stroke-width="2"/>')
    d.text(500, 131, 'FUNC + MIDI', 15, ACC, bold=True)
    d.text(500, 240, 'Shared by both: the master section (freeze delay, EQ, bass mono, limiter), the tempo, projects, the card, USB, the screensavers', 12.5)
    return d.svg('The two faces of AERIA OS')


def step_marks():
    """The marks a step can carry on the sequencer's tall row, enlarged: 7 x 6 pixels each."""
    d = D(1000, 150)
    S = 11
    def blk(x0, y0, px):
        for (x, y) in px:
            d.o.append(f'<rect x="{x0 + x * S}" y="{y0 + y * S}" width="{S + 0.5}" height="{S + 0.5}" fill="#fff"/>')
    full = [(x, y) for x in range(7) for y in range(6)]
    marks = [('A trig', full, 0), ('Locks', [p for p in full if p not in ((3, 2), (3, 3))], 0),
             ('Chance or|condition', [p for p in full if p not in ((6, 0), (6, 1), (5, 0))], 0), ('Ratchet', [p for p in full if p not in ((2, 5), (4, 5))], 0),
             ('Plays|backward', [p for p in full if p != (0, 0)], 0), ('Late|(micro-timing)', full, 1), ('Empty step', [(3, 5)], 0), ('Empty,|on a beat', [(3, 5), (3, 4)], 0)]
    for i, (name, px, dx) in enumerate(marks):
        x0 = 22 + i * 123
        d.o.append(f'<rect x="{x0 - 8}" y="8" width="{9 * S + 6}" height="{6 * S + 16}" rx="4" fill="{theme.SCREEN}" stroke="{theme.SCREEN_EDGE}" stroke-width="1.5"/>')
        blk(x0 + S * (1 if not dx else 2) - S, 16, px)
        d.text(x0 + 44, 112, name, 13, INK)
    return d.svg('The marks on a step')


def ratchet_curve():
    """A ratchet of eight hits inside one step: even, bouncing in (CURV IN), bouncing out (CURV OUT)."""
    d = D(1000, 190)
    def row(y, title, gaps):
        d.text(150, y + 5, title, 13.5, INK, 'end', True)
        x0, x1 = 170, 960
        d.o.append(f'<rect x="{x0}" y="{y - 16}" width="{x1 - x0}" height="32" rx="4" fill="{TINT}" stroke="{FAINT}" stroke-width="1"/>')
        tot = sum(gaps); x = x0
        for g in gaps:
            d.o.append(f'<rect x="{x}" y="{y - 16}" width="5" height="32" fill="{INK}"/>')
            x += (x1 - x0) * g / tot
    r = 0.72
    even = [1] * 8
    inn = [r ** k for k in range(8)]
    out = list(reversed(inn))
    row(40, 'EVEN', even); row(95, 'IN 5', inn); row(150, 'OUT 5', out)
    d.text(565, 184, 'one step (a sixteenth note) from left to right: RTCH 8', 12)
    return d.svg('Ratchets and their curve')


def warp():
    """WARP: the steps inside the loop grow or shrink; the loop keeps its length."""
    d = D(1000, 150)
    import math
    def row(y, title, w):
        d.text(150, y + 5, title, 13.5, INK, 'end', True)
        x0, x1 = 170, 960; n = 16
        ratio = 2 ** (2 * w / 64 / (n - 1))
        gaps = [ratio ** k for k in range(n)]; tot = sum(gaps); x = x0
        for k, g in enumerate(gaps):
            wd = (x1 - x0) * g / tot
            d.o.append(f'<rect x="{x + 1}" y="{y - 14}" width="{wd - 2}" height="28" rx="3" fill="{INK if k % 4 == 0 else theme.D_GREY}"/>')
            x += wd
    row(28, 'WARP -', 0); row(72, 'WARP +32', 32); row(116, 'WARP -32', -32)
    return d.svg('WARP')


def smear():
    """SMEAR (the spectral one, 12 Nov 2026): the wet path through the transform, the dry path beside it."""
    d = D(1000, 250)
    d.band(100, 8, 792, 150, 'THE WET PATH: 1,280 SAMPLES (29 MS) LATE')
    d.box(8, 66, 64, 56, 'IN', 'L R', 'dark')
    d.box(116, 56, 132, 76, 'Transform', '1,024 points,|4 frames overlap,|512 bands a side', fs=14)
    d.box(272, 56, 100, 76, 'GATE', 'quiet bands|out', 'acc', fs=14)
    d.box(396, 56, 108, 76, 'SMR', 'each band held,|then fading', 'acc', fs=14)
    d.box(528, 56, 108, 76, 'FRZ', 'live down,|the held in', 'acc', fs=14)
    d.box(660, 56, 122, 76, 'Back to sound', 'the wash: scattered|phases; WIDE: its|own on each side', fs=14)
    d.box(806, 56, 72, 76, 'TILT', 'around|1 kHz', 'acc', fs=14)
    d.box(912, 56, 80, 76, 'MIX', 'dry / wet', 'dark', fs=14)
    for a, b in ((72, 116), (248, 272), (372, 396), (504, 528), (636, 660), (782, 806), (878, 912)):
        d.arrow([(a, 94), (b, 94)])
    d.arrow([(40, 122), (40, 204), (952, 204), (952, 132)], 'b')
    d.text(500, 196, 'the dry path: not delayed', 12.5, BLUE)
    d.text(500, 228, 'SMR and FRZ follow the weather: the snow freezes, the heat smears.', 12.5)
    return d.svg('SMEAR: transform, gate, sustain, freeze, back to sound, tilt, mix')



def hookup():
    """The quick guide's connection diagram: what goes into which socket. Sound in white, MIDI in blue, USB in orange."""
    d = D(1000, 462)
    d.band(8, 8, 252, 352, 'WHAT GOES IN')
    d.band(740, 8, 252, 352, 'WHAT COMES OUT')
    d.box(322, 22, 356, 338, '', None, 'dark')
    d.text(500, 54, 'OCTATRACK MKII', 19, SLAB_TXT, bold=True)
    d.text(500, 76, 'running AERIA OS', 15, SLAB_SOFT)
    rows = [112, 166, 220, 274, 328]
    left = [('Synth, drum machine,|microphone preamp', None, 'INPUTS A B', ''), ('A second instrument,|or a stereo return', None, 'INPUTS C D', ''),
            ('Clock, controller,|another sequencer', None, 'MIDI IN', 'b'), ('CompactFlash card', 'your WAV files', 'CARD SLOT', 'g'),
            ('Power supply', 'the unit&#8217;s own', '12 V DC', 'g')]
    for y, (t, sb, port, k) in zip(rows, left):
        d.box(20, y - 23, 228, 46, t, sb, 'blue' if k == 'b' else 'tint' if k == 'g' else 'plain', fs=14.5, sfs=13.5)
        if k == 'g':
            d.line([(248, y), (322, y)], FAINT, dash=True)
        else:
            d.arrow([(248, y), (322, y)], k)
        d.text(336, y + 5, port, 15, SLAB_TXT, 'start', True)
    right = [('MAIN OUT L R', 'Mixer, speakers', 'after the master section', ''), ('CUE OUT L R', 'A second pair', 'quad, or a cue mix', ''),
             ('HEADPHONES', 'Headphones', 'main and cue together', ''), ('MIDI OUT', 'Synth, drum machine', 'notes, clock, programs', 'b'),
             ('MIDI THRU', 'The next device', 'a copy of MIDI In', 'b')]
    for y, (port, t, sb, k) in zip(rows, right):
        d.box(752, y - 23, 228, 46, t, sb, 'blue' if k == 'b' else 'plain', fs=14.5, sfs=13.5)
        d.arrow([(678, y), (752, y)], k)
        d.text(664, y + 5, port, 15, SLAB_TXT, 'end', True)
    d.box(290, 402, 420, 52, 'Computer', 'audio, 6 or 18 channels  ·  the card as a disk  ·  update tools', 'acc', fs=15.5, sfs=13.5)
    d.o.append(f'<path d="M500 364 V398" fill="none" stroke="{ACC}" stroke-width="2" marker-end="url(#aha)" marker-start="url(#aha)"/>')
    d.text(512, 387, 'USB', 15, ACC, 'start', True)
    d.line([(20, 392), (64, 392)]); d.text(74, 397, 'sound', 14, INK, 'start')
    d.line([(20, 416), (64, 416)], BLUE); d.text(74, 421, 'MIDI, on the 5-pin sockets', 14, BLUE, 'start')
    d.line([(20, 440), (64, 440)], ACC); d.text(74, 445, 'USB', 14, ACC, 'start')
    d.text(866, 410, 'There is no MIDI over USB:|MIDI is on the three|round sockets.', 14)
    return d.svg('What to connect where')


def usb_formats():
    """The two formats of the USB audio interface, channel by channel."""
    d = D(1000, 252)
    d.text(14, 24, '6 CHANNELS  ·  the format a computer picks by itself', 15, BRIGHT, 'start', True)
    names6 = [('1', 'Input A', 'plain'), ('2', 'Input B', 'plain'), ('3', 'Input C', 'plain'), ('4', 'Input D', 'plain'), ('5', 'MAIN L', 'dark'), ('6', 'MAIN R', 'dark')]
    for i, (n, t, k) in enumerate(names6):
        d.box(14 + i * 108, 38, 100, 58, n, t, k, fs=18, sfs=15)
    d.text(676, 62, 'your playing as it comes in,|and AERIA&#8217;s result, side by side', 15, SOFT, 'start')
    d.text(14, 142, '18 CHANNELS  ·  choose it on the computer: stems of the sequencer', 15, BRIGHT, 'start', True)
    d.box(14, 156, 100, 58, '1-2', 'MAIN L R', 'dark', fs=18, sfs=15)
    for i in range(8):
        d.box(122 + i * 108, 156, 100, 58, f'{3 + i * 2}-{4 + i * 2}', f'Track {i + 1}', 'acc', fs=18, sfs=15)
    d.text(500, 242, 'a stem is its track after its filters and its effect, before its pan; the master section is in the MAIN pair only', 14.5)
    return d.svg('The two USB audio formats')


def seq_map():
    """The quick guide's map of the sequencer: the keys that open its knob pages, and the knobs on each."""
    d = D(1000, 322)
    pages = [('SRC', 'SRC', 'SMPL  PTCH  STRT|REV  MACH  SLIP', 'RTIO  INDX  IDEC'), ('AMP', 'AMP', 'ATK  DEC  VOL|PAN', 'PENV  PDEC  DUCK'),
             ('LFO', 'MOD', 'WAVE  DEST|RATE  DPTH', ''), ('FX1', 'FLT', 'HP  LP  DLY|OUT  DUCK', ''),
             ('FX2', 'FX', 'the six knobs of|the track&#8217;s effect', 'the effect&#8217;s second set'), ('ARR', 'GEN', 'DENS  DIR  ROT|WARP  RATE  SHUF', ''),
             ('MIDI', 'MIDI', 'MCH  NOTE  CHRD|GATE  PROG', 'SYNC  PCCH'), ('PAGE', 'PTN', 'LEN  PLEN  SWNG|TMPO  DSRC  DREL', 'FREE')]
    for i, (key, page, knobs, second) in enumerate(pages):
        x = 6 + (i % 4) * 249; y = 6 + (i // 4) * 142
        d.o.append(f'<rect x="{x}" y="{y}" width="241" height="132" rx="7" fill="{SLAB}" stroke="{SLAB_EDGE}" stroke-width="1.6"/>')
        d.o.append(f'<rect x="{x + 12}" y="{y + 12}" width="70" height="32" rx="6" fill="{theme.KEY_BG}" stroke="{theme.KEY_EDGE}" stroke-width="1.8"/>')
        d.text(x + 47, y + 34, key, 17, BRIGHT, bold=True)
        d.text(x + 96, y + 36, page, 23, SLAB_TXT, 'start', True)
        for j, l in enumerate(tr(knobs).split('|')):
            d.text(x + 14, y + 70 + j * 21, l, 16.5, SLAB_SOFT, 'start')
        if second:
            d.text(x + 14, y + 119, '2:  ' + tr(second), 15.5, SLAB_ACC, 'start', True)
    d.text(500, 306, 'The key opens the page.  The same key again, or FUNC held: its second set (orange).  A step held: VEL MICR PROB COND RTCH CURV.', 15)
    return d.svg('The sequencer knob pages')
