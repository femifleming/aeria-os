#!/usr/bin/env python3
"""Builds the AERIA OS manual: body.html (this folder) + panel.py + diagrams.py -> aeria-os/manual/aeria_os_manual.html
and, with --pdf, AERIA_OS_Manual.pdf through headless Chrome.

body.html is the manual's text only: chapters are <h2 id="...">, sections <h3>, sub-sections <h4>. The numbering,
the table of contents, the running heads, the page numbers, the cross-references and the index are made here.

Macros in body.html
  [[S name | caption]]                     one screenshot (img/name.png)
  [[G3]] name | caption ... [[/G]]         a grid of screenshots, 2 or 3 across
  [[ANNO name | caption                    a screenshot with call-outs: x,y (or x,y,w,h: a frame) in screen pixels,
  x,y | L | text ... ]]                    the side the label goes (L R T B), the label ("/" breaks the line)
  [[PANEL spec | title                     the panel with the controls of `spec` lit (see panel.py: mini), and beside
  KEYS :: what they do ... ]]              it a list: the keys, what they do here
  [[LAMPS levels | tags | caption]]        the panel in the dark with lamps at brightness 0..3
  [[TRIGS 2001... | caption | 1=kick 5=..]] a row of sixteen trig lamps
  {{FRONT}} {{REAR}} {{FLOW_SKY}} ...      drawings (panel.py, diagrams.py)
  {{TOC}} {{INDEX}} {{SAVERS}}             made here ({{SAVERS}}: the screensavers' names, read from screensavers.c)
  {{ch:id}}  {{pg:id}}                     "chapter 12 (page 80)" / "page 80", linked
  {{ix:term}}                              an index entry at this place
  [FUNC] [T1] ...                          a key, drawn as a key-cap;  ((LEVEL)) ((A))  a knob, bold italic
A plain "chapter N" left in the text is read as a chapter number of the edition before this one (the screensaver
chapter is still being written against it) and is turned into the same kind of link.

Themes and languages: --theme dark|light (theme.py) and --lang en|zh|ja|de (lang.py). English is body.html, a
translation body.<lang>.html, with the same markup and macros and the same ids. What this builder writes in words
(running heads, the cross-references, "Appendix", the index's headings) comes from lang.py. The output:
  aeria-os/manual/<lang>/AERIA_OS_Manual_<Dark|Light>.pdf (and its .html beside it, aeria_os_manual_<theme>.html),
  and for English in the dark theme also aeria-os/manual/AERIA_OS_Manual.pdf and aeria_os_manual.html, as before.
build_all.py builds every language in both themes.

Usage: build_manual.py [--pdf] [--test] [--lang en|zh|ja|de] [--theme dark|light] [--body other_body.html]"""
import re, os, sys, subprocess, html, shutil, tempfile, importlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import panel, diagrams, theme, lang

here = os.path.dirname(os.path.abspath(__file__))
man_dir = os.path.expanduser('~/octa/aeria-os/manual')
img_dir = os.path.join(man_dir, 'img')
CHROME = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'
BUILD_NAME = 'AERIA OS, 12 November 2026'      # the English; a translation's is lang.S('build_name')
used = []
FONT_EN = '"Helvetica Neue", Helvetica, Arial, sans-serif'


def set_theme(name):
    """Use the theme `name` (theme.py) from here on: the stylesheet, and the drawings of panel.py and diagrams.py,
    which read their colours when they are loaded, so they are loaded again."""
    theme.use(name)
    importlib.reload(panel); importlib.reload(diagrams)


def set_lang(code):
    lang.use(code)
    importlib.reload(diagrams)


def arg(name, default):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default


def source(kind, code):
    """The text of a document in a language: body.html or quick.html for English, body.<code>.html ... otherwise."""
    return os.path.join(here, f'{kind}.html' if code == 'en' else f'{kind}.{code}.html')


def out_paths(code, theme_name, test=False, kind='manual'):
    """Where a build goes: (html, pdf, tmp_html, tmp_pdf, extra copies of the pdf, of the html).
    Each language has its own folder with an img link to the shared pictures; English in the dark theme is also
    written where it always was."""
    stem, pdfname = ('aeria_os_manual', 'AERIA_OS_Manual') if kind == 'manual' else ('aeria_os_quick_guide', 'AERIA_OS_Quick_Guide')
    tag = f'{code}_{theme_name}'
    if test:
        return (os.path.join(man_dir, f'_test_{kind}_{tag}.html'), os.path.join(man_dir, f'_test_{kind}_{tag}.pdf'),
                os.path.join(man_dir, f'_pass_{kind}_{tag}.html'), os.path.join(man_dir, f'_pass_{kind}_{tag}.pdf'), [], [])
    d = os.path.join(man_dir, code)
    os.makedirs(d, exist_ok=True)
    link = os.path.join(d, 'img')
    if not os.path.lexists(link):
        os.symlink('../img', link)
    html_out = os.path.join(d, f'{stem}_{theme_name}.html')
    pdf = os.path.join(d, f'{pdfname}_{theme_name.capitalize()}.pdf')
    extra_pdf, extra_html = [], []
    if code == 'en' and theme_name == 'dark':
        extra_pdf.append(os.path.join(man_dir, f'{pdfname}.pdf')); extra_html.append(os.path.join(man_dir, f'{stem}.html'))
    return html_out, pdf, os.path.join(d, f'_pass_{kind}_{theme_name}.html'), os.path.join(d, f'_pass_{kind}_{theme_name}.pdf'), extra_pdf, extra_html


def stylesheet(css_extra=''):
    """The manual's stylesheet in the theme and the language in use (build_quick.py adds its own to it)."""
    css = theme.fill(CSS.replace('/*THEME*/', theme.css_root()))
    return css.replace(FONT_EN, lang.font()) + '\n' + css_extra + '\n' + lang.css()


def margin_box_css():
    """The running heads' boxes (an @page rule cannot read the variables of :root)."""
    return (f'font: 700 7.6pt {lang.font()}; letter-spacing: {"0" if lang.cjk() else "0.05em"}; color: {theme.SOFT}; border-bottom: 0.6pt solid {theme.SOFT}; '
            'vertical-align: bottom; padding-bottom: 2.2mm; margin-bottom: 7mm; content: "";')

KEYS = ('FUNC|MIDI|REC1|REC2|REC3|PROJ|PART|AED|MIX|ARR|CUE|PTN|BANK|YES|NO|UP|DOWN|LEFT|RIGHT|SRC|AMP|LFO|FX1|FX2|REC|PLAY|STOP|'
        'TEMPO|PAGE|SCENE A|SCENE B|T[1-8]|TRIG|TRIGS|TRACK|TRIG \\d+(?:[-–]\\d+)?|T[1-8][-–]T?[1-8]|TRIG \\d+ to \\d+')
OLD_CH = {1: 'intro', 2: 'quick', 3: 'panel', 4: 'controls', 5: 'sky', 6: 'chain', 7: 'fx', 8: 'out', 9: 'cloud', 10: 'sources', 11: 'card',
          12: 'mix', 13: 'scenes', 14: 'midimod', 15: 'midi', 16: 'seq', 17: 'projects', 18: 'usb', 19: 'system', 20: 'savers', 21: 'update',
          22: 'specs', 23: 'midiimp', 24: 'notyet'}

CSS = r"""
@page { size: A4; margin: 24mm 17mm 22mm 17mm; background: %BG%;
  @bottom-center { content: counter(page); font: 600 9.5pt "Helvetica Neue", Helvetica, Arial, sans-serif; color: %INK%; border-top: 0.6pt solid %SOFT%; padding-top: 2.5mm; vertical-align: top; margin-top: 6mm; width: 100%; }
}
@page cover { margin: 0; @bottom-center { content: none; border: none } @top-left { content: none; border: none } @top-right { content: none; border: none } }
/*THEME*/
* { box-sizing: border-box; }
html { -webkit-print-color-adjust: exact; print-color-adjust: exact; }
body { font-family: "Helvetica Neue", Helvetica, Arial, sans-serif; color: var(--ink); font-size: 10.4pt; line-height: 1.52; background: var(--bg); margin: 0; }
html { background: var(--bg); }
b, strong { color: var(--bright); }
section.chapter { break-before: page; }
h1 { font-size: 30pt; letter-spacing: 0.32em; font-weight: 300; margin: 0; }
h2 { font-size: 17pt; font-weight: 700; text-transform: uppercase; letter-spacing: 0.01em; margin: 0 0 12px; padding-bottom: 5px; border-bottom: 1.6px solid var(--ink); break-after: avoid; line-height: 1.2; }
h3 { font-size: 12.4pt; font-weight: 700; text-transform: uppercase; margin: 24px 0 7px; break-after: avoid; line-height: 1.25; }
h4 { font-size: 10.4pt; font-weight: 700; text-transform: uppercase; margin: 16px 0 4px; break-after: avoid; letter-spacing: 0.02em; }
h2 .num, h3 .num, h4 .num { margin-right: 0.45em; }
h3.pb { break-before: page; margin-top: 0; }
p { margin: 7px 0; orphans: 3; widows: 3; }
ul, ol { margin: 7px 0; padding-left: 21px; } li { margin: 3.5px 0; }
ol.steps > li { margin: 7px 0; padding-left: 3px; } ol.steps > li::marker { font-weight: 700; }
a { color: inherit; text-decoration: none; }
a.xref { white-space: nowrap; } a.xref .pg { color: var(--soft); font-size: 0.92em; }
small { color: var(--soft); }
.mk { position: absolute; font-size: 1px; line-height: 1px; color: var(--bg); width: 0; height: 0; overflow: visible; white-space: nowrap; letter-spacing: 0; text-transform: none; font-weight: 400; }

/* the cover and the front matter */
.cover { page: cover; height: 297mm; display: flex; flex-direction: column; justify-content: center; align-items: center; text-align: center; break-after: page; background: var(--bg); }
img[src$="aeria_logo.png"] { filter: var(--logo-filter); }   /* the dark theme turns the black logo white */
.cover .sub { font-size: 12pt; color: var(--soft); letter-spacing: 0.25em; margin-top: 12px; }
.cover .by { margin-top: 54px; font-size: 10pt; letter-spacing: 0.3em; color: var(--soft); }
.cover .ed { margin-top: 70px; font-size: 9pt; color: var(--soft); letter-spacing: 0.08em; }
.fh { font-size: 17pt; font-weight: 700; text-transform: uppercase; margin: 0 0 12px; padding-bottom: 5px; border-bottom: 1.6px solid var(--ink); line-height: 1.2; }
tr:has(> td) > th { background: var(--tint); color: var(--ink); width: 15%; text-transform: none; font-size: 9pt; letter-spacing: 0; border-bottom: 0.8px solid var(--rule); }
.toc { margin-top: 6px; columns: 2; column-gap: 22px; }
.toc > div { break-inside: avoid; }
.toc a { display: flex; align-items: baseline; }
.toc .l1 { font-weight: 700; font-size: 10.6pt; margin-top: 7px; text-transform: uppercase; break-after: avoid; }
.toc .l2 { font-size: 8.9pt; padding-left: 15px; text-transform: uppercase; color: var(--ink); line-height: 1.5; }
.toc .part { font-size: 8.4pt; letter-spacing: 0.22em; color: var(--accent); font-weight: 700; margin: 13px 0 0; text-transform: uppercase; break-after: avoid; }
.toc .dots { flex: 1; border-bottom: 1.2px dotted var(--faint); margin: 0 5px; transform: translateY(-3px); }
.toc .n { font-variant-numeric: tabular-nums; min-width: 22px; text-align: right; }
.partpage { break-before: page; height: 225mm; display: flex; flex-direction: column; justify-content: center; }
.partpage .pn { font-size: 10pt; letter-spacing: 0.3em; color: var(--accent); font-weight: 700; text-transform: uppercase; }
.partpage .pt { font-size: 34pt; font-weight: 300; letter-spacing: 0.04em; margin: 8px 0 14px; line-height: 1.1; }
.partpage .pd { font-size: 11.5pt; color: var(--soft); max-width: 125mm; }
.partpage .pl { margin-top: 22px; font-size: 10pt; line-height: 1.9; }

/* tables */
table { border-collapse: collapse; width: 100%; margin: 8px 0 12px; font-size: 9.3pt; line-height: 1.42; }
tr { break-inside: avoid; } thead { display: table-header-group; }
table:not(.long) { break-inside: avoid; }
th, td { text-align: left; vertical-align: top; padding: 5px 8px; border-bottom: 0.8px solid var(--rule); }
th { background: var(--head); color: var(--head-ink); font-weight: 700; font-size: 8.4pt; text-transform: uppercase; letter-spacing: 0.04em; border-bottom: none; }
tbody tr:nth-child(even) > td, table:not(:has(thead)) tr:nth-child(odd) > td { background: var(--zebra); }
table:has(thead) tbody tr:nth-child(odd) > td { background: transparent; }
td.k, th.k { white-space: nowrap; font-weight: 700; font-size: 9pt; letter-spacing: 0.02em; }
td.n { white-space: nowrap; font-variant-numeric: tabular-nums; }
table.game th { width: 17%; background: var(--tint); color: var(--ink); border-bottom: 0.8px solid var(--rule); }
table.knobs td.pos { width: 34px; text-align: center; font-weight: 700; color: var(--bg); background: var(--ink) !important; border-bottom: 0.8px solid var(--bg); }
table.knobs td.pos.f { background: var(--accent) !important; color: var(--on-accent); }
table.combo { table-layout: fixed; }
table.combo th:first-child, table.combo td:first-child { width: 37%; line-height: 1.75; }
table.combo:has(th:nth-child(3)) th:first-child { width: 26%; } table.combo:has(th:nth-child(3)) th:nth-child(3) { width: 24%; }

/* keys, knobs, parameters, messages */
.key { display: inline-block; font-weight: 700; font-size: 0.8em; letter-spacing: 0.03em; border: 1px solid var(--key-edge); border-bottom-width: 2px; border-radius: 4px; padding: 0 4.5px; background: var(--key-bg); white-space: nowrap; line-height: 1.38; vertical-align: 0.06em; color: var(--bright); }
th .key { color: var(--bright); }
i.knob { font-weight: 700; font-style: italic; white-space: nowrap; }
.msg { font-family: "SF Mono", Menlo, monospace; font-size: 0.86em; background: var(--screen); color: #fff; padding: 0.5px 5px; border-radius: 2px; white-space: nowrap; letter-spacing: 0.03em; border: 0.8px solid var(--screen-edge); }
.led { white-space: nowrap; font-weight: 600; }

/* boxes */
.note, .tip, .warn { display: flex; gap: 11px; align-items: flex-start; padding: 9px 12px 9px 10px; margin: 11px 0; font-size: 9.5pt; break-inside: avoid; border-radius: 3px; line-height: 1.46; }
.note, .tip { background: var(--tint); border-left: 3.5px solid var(--blue); }
.warn { background: var(--accent-ground); border-left: 3.5px solid var(--accent); }
.note::before, .tip::before, .warn::before { flex: 0 0 auto; width: 19px; height: 19px; border-radius: 50%; color: var(--bg); font-weight: 700; font-size: 10pt; display: flex; align-items: center; justify-content: center; margin-top: 1px; }
.note::before, .tip::before { content: "i"; background: var(--blue); font-family: Georgia, serif; font-style: italic; }
.tip::before { content: "\2713"; font-family: inherit; font-style: normal; font-size: 9pt; }
.warn::before { content: "!"; background: var(--accent); }
.note > div, .tip > div, .warn > div { flex: 1; }

/* figures */
figure { margin: 0; break-inside: avoid; }
figcaption { font-size: 8.5pt; color: var(--soft); margin-top: 4px; text-align: center; line-height: 1.35; }
figure.shot img { display: block; width: 100%; image-rendering: pixelated; border: 2.5px solid var(--screen-edge); border-radius: 4px; background: var(--screen); }
figure.one { width: 58%; margin: 10px auto 12px; }
.keep figure.one { width: 46%; }
.shots { display: grid; gap: 10px 13px; margin: 10px 0 12px; }
.shots.c2 { grid-template-columns: 1fr 1fr; padding: 0 5%; } .shots.c3 { grid-template-columns: 1fr 1fr 1fr; } .shots.c4 { grid-template-columns: 1fr 1fr 1fr 1fr; }
svg.panel, svg.diagram, svg.anno, svg.trigrow { display: block; width: 100%; height: auto; }
figure.draw { margin: 10px 0 14px; }
figure.anno { margin: 12px 0 14px; }
figure.trigs { margin: 9px 6% 12px; }
.keep { break-inside: avoid; }
.pfig { display: flex; gap: 14px; align-items: flex-start; margin: 10px 0 14px; break-inside: avoid; padding: 10px; border: 0.8px solid var(--rule); border-radius: 4px; background: var(--zebra); }
.pfig .pp { flex: 0 0 57%; }
.pfig .pl { flex: 1; font-size: 8.9pt; line-height: 1.38; }
.pfig .pt { font-weight: 700; text-transform: uppercase; font-size: 8.6pt; letter-spacing: 0.05em; color: var(--accent); margin-bottom: 5px; }
.pfig .row { display: flex; gap: 7px; padding: 3.2px 0; border-bottom: 0.6px solid var(--rule); }
.pfig .row:last-child { border-bottom: none; }
.pfig .row .kk { flex: 0 0 39%; }
.pfig.wide { display: block; } .pfig.wide .pp { width: 74%; margin: 0 auto 8px; } .pfig.wide .pl { columns: 2; column-gap: 18px; } .pfig.wide .row { break-inside: avoid; }
.pfig.wide .row .kk { flex: 0 0 34%; }
.step { display: flex; gap: 14px; align-items: flex-start; margin: 9px 0; break-inside: avoid; padding-bottom: 9px; border-bottom: 0.6px solid var(--rule); }
.step .sn { flex: 0 0 26px; height: 26px; border-radius: 50%; background: var(--ink); color: var(--bg); font-weight: 700; display: flex; align-items: center; justify-content: center; font-size: 10.5pt; }
.step .st { flex: 1; } .step .st p:first-child { margin-top: 2px; }
.step .sp { flex: 0 0 31%; } .step .ss { flex: 0 0 27%; }
.step .ss img { display: block; width: 100%; image-rendering: pixelated; border: 2px solid var(--screen-edge); border-radius: 3px; background: var(--screen); }
.fxhead { display: flex; gap: 12px; align-items: baseline; }
.fxb { break-inside: avoid; }
.fxb > p { break-after: avoid; }
.cols2 { columns: 2; column-gap: 22px; }
.index { columns: 3; column-gap: 16px; font-size: 8.3pt; line-height: 1.42; }
.index .il { font-weight: 700; font-size: 10.5pt; margin: 9px 0 2px; break-after: avoid; color: var(--accent); }
.index div.ie { break-inside: avoid; padding-left: 9px; text-indent: -9px; }
.index .ie .pn { color: var(--soft); }
.endmark { margin-top: 30px; text-align: center; }
"""


def esc(s):
    return html.escape(s, quote=True)


def keycaps(s):
    """[FUNC] -> a key-cap; ((LEVEL)) -> a knob's name; ``TEXT`` -> a message on the screen; <<PLAY>> -> a lamp."""
    s = re.sub(r'\[(' + KEYS + r')\]', lambda m: f'<span class="key">{m.group(1)}</span>', s)
    s = re.sub(r'\(\(([A-Z][A-Za-z ]*)\)\)', lambda m: f'<i class="knob">{m.group(1)}</i>', s)
    s = re.sub(r'``([^`]+)``', lambda m: f'<span class="msg">{m.group(1)}</span>', s)
    s = re.sub(r'&lt;&lt;([A-Z0-9 :-]+)&gt;&gt;|<<([A-Z0-9 :-]+)>>', lambda m: f'<span class="led">&lt;{m.group(1) or m.group(2)}&gt;</span>', s)
    return s


def fig(name, cap, cls='shot'):
    name = name.strip(); cap = cap.strip()
    assert os.path.exists(os.path.join(img_dir, name + '.png')), 'no screenshot: ' + name
    used.append(name)
    return f'<figure class="{cls}"><img src="img/{name}.png" alt=""><figcaption>{cap}</figcaption></figure>'


def grid(m):
    n = m.group(1)
    rows = [l for l in m.group(2).strip().split('\n') if l.strip()]
    return f'<div class="shots c{n}">' + ''.join(fig(*l.split('|', 1)) for l in rows) + '</div>'


def anno(m):
    """A screenshot with call-outs. Screen pixels (128 x 64) are 4 units; the picture sits in the middle of a 1000-wide drawing."""
    name, cap = [x.strip() for x in m.group(1).split('|', 1)]
    assert os.path.exists(os.path.join(img_dir, name + '.png')), 'no screenshot: ' + name
    used.append(name)
    items = {'L': [], 'R': [], 'T': [], 'B': []}
    for l in m.group(2).strip().split('\n'):
        if not l.strip():
            continue
        pos, side, text = [x.strip() for x in l.split('|', 2)]
        items[side].append(([float(v) for v in pos.split(',')], text))
    SC = 4.0; SW, SH = 128 * SC, 64 * SC
    X0 = 244; top_h = 0; LH = 18
    def lines(t, width=31):
        # the breaks written with "/", and any line still too long for the margin is broken at a space
        out = []
        for part in t.split('/'):
            part = part.strip(); plain = lambda x: re.sub(r'\(\(|\)\)|\[|\]|<[^>]+>', '', x)
            if lang.width(plain(part)) == len(plain(part)):          # Latin only: as before
                while len(plain(part)) > width:
                    cut = part.rfind(' ', 0, width + (len(part) - len(plain(part))) // 2)
                    if cut <= 0:
                        break
                    out.append(part[:cut]); part = part[cut + 1:]
                out.append(part)
                continue
            out.extend(wrap_cjk(part, width))
        return out
    if items['T']:
        top_h = 22 + max(len(lines(t)) for _, t in items['T']) * LH
    Y0 = 18 + top_h
    bot_h = (26 + max(len(lines(t, 16 if len(items['B']) > 3 else 60)) for _, t in items['B']) * LH) if items['B'] else 0
    Hh = Y0 + SH + bot_h + 8
    acc = theme.ACC
    o = f'<rect x="{X0 - 5}" y="{Y0 - 5}" width="{SW + 10}" height="{SH + 10}" rx="5" fill="{theme.SCREEN}" stroke="{theme.SCREEN_EDGE}" stroke-width="2.5"/>'
    o += f'<image href="img/{name}.png" x="{X0}" y="{Y0}" width="{SW}" height="{SH}" style="image-rendering: pixelated" preserveAspectRatio="none"/>'
    over = ''
    def target(pos):
        if len(pos) == 4:
            x, y, w, h = pos
            r = (X0 + x * SC - 3, Y0 + y * SC - 3, w * SC + 6, h * SC + 6)
            return r, f'<rect x="{r[0]}" y="{r[1]}" width="{r[2]}" height="{r[3]}" rx="3" fill="none" stroke="{acc}" stroke-width="2.4"/>'
        x, y = pos
        cx, cy = X0 + x * SC + 2, Y0 + y * SC + 2
        return (cx, cy, 0, 0), f'<circle cx="{cx}" cy="{cy}" r="5.5" fill="{acc}" stroke="#fff" stroke-width="1.6"/>'
    def label(x, y, t, anchor, width=31):
        s = ''
        for i, ln in enumerate(lines(t, width)):
            ln = keycaps(ln.strip()); ln = re.sub(r'<span class="key">(.*?)</span>', r'<tspan font-weight="700">\1</tspan>', ln); ln = re.sub(r'<b>(.*?)</b>', r'<tspan font-weight="700">\1</tspan>', ln); ln = re.sub(r'<i class="knob">(.*?)</i>', r'<tspan font-weight="700" font-style="italic">\1</tspan>', ln)
            s += f'<text x="{x}" y="{y + i * LH}" text-anchor="{anchor}" font-size="15" fill="{theme.INK}">{ln}</text>'
        return s
    for side in 'LR':
        its = sorted(items[side], key=lambda it: it[0][1] + (it[0][3] / 2 if len(it[0]) == 4 else 0))
        if not its:
            continue
        heights = [len(lines(t)) * LH + 9 for _, t in its]
        total = sum(heights)
        # each label as near its target's height as the others allow
        ys = []; y = Y0 - 2
        want = [Y0 + (p[1] + (p[3] / 2 if len(p) == 4 else 0.5)) * SC - (h - 9) / 2 + 4 for (p, _), h in zip(its, heights)]
        for w_, h in zip(want, heights):
            y = max(y, w_); ys.append(y); y += h
        over_by = y - (Y0 + SH + 6)
        if over_by > 0:
            for i in range(len(ys) - 1, -1, -1):
                lim = (ys[i + 1] - heights[i]) if i + 1 < len(ys) else (Y0 + SH + 6 - heights[i])
                ys[i] = min(ys[i], lim)
            y = Y0 - 2                                   # ... but not above the picture's top
            for i in range(len(ys)):
                ys[i] = max(ys[i], y); y = ys[i] + heights[i]
            Hh = max(Hh, y + bot_h + 4)
        for ((pos, text), yl, h) in zip(its, ys, heights):
            r, mark = target(pos)
            ly = yl + 5 + (len(lines(text)) - 1) * LH / 2
            if side == 'L':
                lx = X0 - 22; tx, ty = (r[0], min(max(ly, r[1]), r[1] + r[3])) if r[2] else (r[0], r[1])
                over += f'<path d="M{lx + 5} {ly - 5} H{X0 - 9} L{tx} {ty}" fill="none" stroke="{acc}" stroke-width="1.7"/>' + mark
                o += label(lx, yl + 5, text, 'end')
            else:
                lx = X0 + SW + 22; tx, ty = (r[0] + r[2], min(max(ly, r[1]), r[1] + r[3])) if r[2] else (r[0], r[1])
                over += f'<path d="M{lx - 5} {ly - 5} H{X0 + SW + 9} L{tx} {ty}" fill="none" stroke="{acc}" stroke-width="1.7"/>' + mark
                o += label(lx, yl + 5, text, 'start')
    for side in 'TB':
        its = sorted(items[side], key=lambda it: it[0][0])
        n = len(its)
        for i, (pos, text) in enumerate(its):
            r, mark = target(pos)
            tx = r[0] + r[2] / 2
            lx = X0 + (pos[0] + (pos[2] / 2 if len(pos) == 4 else 0.5)) * SC if n > 6 else X0 + SW * (i + 0.5) / n
            if side == 'B':
                ly = Y0 + SH + 30
                over += f'<path d="M{lx} {ly - 15} L{tx} {r[1] + r[3]}" fill="none" stroke="{acc}" stroke-width="1.7"/>' + mark
                o += label(lx, ly, text, 'middle', 16 if n > 3 else 60)
            else:
                ly = 14
                nl = len(lines(text))
                over += f'<path d="M{lx} {ly + (nl - 1) * LH + 7} L{tx} {r[1]}" fill="none" stroke="{acc}" stroke-width="1.7"/>' + mark
                o += label(lx, ly, text, 'middle')
    svg = f'<svg class="anno" viewBox="0 0 1000 {Hh}" xmlns="http://www.w3.org/2000/svg" font-family="Helvetica Neue, Helvetica, Arial, sans-serif" role="img">{o}{over}</svg>'
    return f'<figure class="anno">{svg}<figcaption>{cap}</figcaption></figure>'


NO_START = set('、。，．・：；？！）」』】〕〉》”’ー々ゃゅょっぁぃぅぇぉャュョッァィゥェォ,.:;!?)]')
NO_END = set('（「『【〔〈《“‘([')


def wrap_cjk(text, width):
    """Breaks a call-out's text with CJK in it into lines of at most `width` (Latin letters; a CJK character is two):
    between CJK characters, or at a space; never inside a macro ([KEY], ((KNOB)), a tag), never before a closing
    sign or after an opening one."""
    toks = re.findall(r'\[\[.*?\]\]|\[[^\]]*\]|\(\([^)]*\)\)|<[^>]+>|[A-Za-z0-9][\w.\-+%]*|\s+|.', text)
    vis = lambda tk: 0 if tk.startswith('<') else lang.width(re.sub(r'^\(\(|\)\)$|^\[|\]$', '', tk))
    lines, cur, w = [], [], 0
    for tk in toks:
        tw = vis(tk)
        if cur and w + tw > width and not tk.isspace() and tk not in NO_START and (cur[-1] not in NO_END):
            lines.append(''.join(cur).strip()); cur, w = [], 0
        if not cur and tk.isspace():
            continue
        cur.append(tk); w += tw
    if cur:
        lines.append(''.join(cur).strip())
    return lines or ['']


def panel_fig(m):
    head = m.group(2).split('\n')[0]
    spec, _, title = [x.strip() for x in head.partition('|')]
    rows = [l for l in m.group(2).split('\n')[1:] if l.strip()]
    wide = m.group(1) == 'W' or len(rows) > 9
    leg = f'<div class="pt">{title}</div>' if title else ''
    for l in rows:
        k, _, v = l.partition('::')
        leg += f'<div class="row"><div class="kk">{k.strip()}</div><div class="vv">{v.strip()}</div></div>'
    return f'<div class="pfig{" wide" if wide else ""}"><div class="pp">{panel.mini(spec)}</div><div class="pl">{leg}</div></div>'


def lamps_fig(m):
    lv, tags, cap = [x.strip() for x in m.group(1).split('|', 2)]
    return f'<figure class="draw" style="margin-left:14%;margin-right:14%">{panel.lamps(lv, tags)}<figcaption>{cap}</figcaption></figure>'


def trigs_fig(m):
    parts = [x.strip() for x in m.group(1).split('|')]
    tags = {int(k): v.replace('_', ' ') for k, v in (t.split('=') for t in parts[2].split())} if len(parts) > 2 and parts[2] else None
    return f'<figure class="trigs">{panel.trig_row(parts[0], tags)}<figcaption>{parts[1]}</figcaption></figure>'


def step_fig(m):
    """[[STEP n | panel spec | screenshot]] text [[/STEP]]: one step of a walk-through."""
    n, spec, shot = [x.strip() for x in m.group(1).split('|')]
    o = f'<div class="step"><div class="sn">{n}</div><div class="st">{m.group(2).strip()}</div>'
    if spec:
        o += f'<div class="sp">{panel.mini(spec)}</div>'
    if shot:
        assert os.path.exists(os.path.join(img_dir, shot + '.png')), 'no screenshot: ' + shot
        used.append(shot)
        o += f'<div class="ss"><img src="img/{shot}.png" alt=""></div>'
    return o + '</div>'


def knobify(m):
    """A knob table written the older way (first cell "A: ROUT", "FUNC A: SCAT", "FUNC: PTCH" or just the name, six
    in a row) gets the knob's letter as a chip, black for the first set and orange for the second, like the newer ones."""
    t = m.group(0)
    if 'class="knobs' in t or 'class="pos' in t:
        return t
    rows = re.findall(r'<tr>.*?</tr>', t, re.S)
    if len(rows) < 2 or not re.search(r'<th[^>]*>\s*(?:\w+ )?[Kk]nob\s*</th>', rows[0]):
        return t
    out = []; n1 = 0; n2 = 0; split = None
    for r in rows[1:]:
        cells = re.findall(r'<td class="k"[^>]*>(.*?)</td>', r, re.S)
        if len(cells) != 1 or not r[4:].lstrip().startswith('<td class="k"') or 'rowspan' in r:
            return t
        c = cells[0].strip()
        mm = re.fullmatch(r'(FUNC )?([A-F])(?:: (.+))?', c, re.S)
        if mm:
            f, letter, name = bool(mm.group(1)), mm.group(2), mm.group(3)
        else:
            mm = re.fullmatch(r'FUNC: (.+)', c, re.S)
            if mm:
                f, name = True, mm.group(1); letter = 'ABCDEF'[n2] if n2 < 6 else ''
            else:
                f, name = False, c; letter = 'ABCDEF'[n1] if n1 < 6 else ''
            if not letter or ' ' in name.replace('(blank)', 'x') or ',' in name:
                return t
        if f: n2 += 1
        else: n1 += 1
        has_name = name is not None
        if split is None: split = has_name
        if split != has_name:
            return t
        chip = f'<td class="pos{" f" if f else ""}">{letter}</td>'
        out.append(re.sub(r'<td class="k"[^>]*>.*?</td>', chip + (f'<td class="k">{name}</td>' if has_name else ''), r, count=1, flags=re.S))
    head = rows[0]
    if split:
        head = head.replace('<tr>', '<tr><th></th>', 1)
        head = re.sub(r'<th[^>]*>\s*(?:\w+ )?[Kk]nob\s*</th>', f'<th>{lang.S("th_name")}</th>', head, count=1)
    else:
        head = re.sub(r'<th[^>]*>\s*(?:\w+ )?[Kk]nob\s*</th>', '<th></th>', head, count=1)
    opening = re.match(r'<table[^>]*>', t).group(0)
    if 'class="' in opening:
        opening = opening.replace('class="', 'class="knobs ', 1)
    else:
        opening = opening.replace('<table', '<table class="knobs"', 1)
    return opening + head + ''.join(out) + '</table>'


def savers_list():
    src = open(os.path.expanduser('~/octa/cloudfin/parts/screensavers.c')).read()
    m = re.search(r'ss_name\[SS_COUNT\][^=]*=\s*\{(.*?)\};', src, re.S)
    names = re.findall(r'"([A-Z0-9 ]+)"', m.group(1))
    return [n for n in names if n != 'OFF']


def macros(body):
    """The pictures, drawings, key-caps and boxes of a body text (the manual's, or the quick guide's: build_quick.py)."""
    # the screenshots and drawings
    body = re.sub(r'\[\[G(\d)\]\](.*?)\[\[/G\]\]', grid, body, flags=re.S)
    body = re.sub(r'\[\[S ([\w]+) \| (.*?)\]\]', lambda m: fig(m.group(1), m.group(2), 'shot one'), body)
    body = re.sub(r'\[\[ANNO ([^\n]*)\n(.*?)\]\]', anno, body, flags=re.S)
    body = re.sub(r'\[\[PANEL(W?) (.*?)\]\]', panel_fig, body, flags=re.S)
    body = re.sub(r'\[\[LAMPS (.*?)\]\]', lamps_fig, body, flags=re.S)
    body = re.sub(r'\[\[TRIGS (.*?)\]\]', trigs_fig, body, flags=re.S)
    body = re.sub(r'\[\[STEP (.*?)\]\](.*?)\[\[/STEP\]\]', step_fig, body, flags=re.S)
    body = body.replace('{{FRONT}}', panel.front()).replace('{{REAR}}', panel.rear())
    body = re.sub(r'\{\{DIAGRAM (\w+)\}\}', lambda m: getattr(diagrams, m.group(1))(), body)
    body = re.sub(r'<table[^>]*>.*?</table>', knobify, body, flags=re.S)
    body = keycaps(body)
    # knobs named in the older text
    body = re.sub(r'<span class="key">(LEVEL|HEADPHONES VOL)</span>', r'<i class="knob">\1</i>', body)
    body = re.sub(lang.KNOB_WORD[lang.CODE], r'\1 <i class="knob">\2</i>', body)
    body = body.replace('<span class="key">PATTERN</span>', '<span class="key">PTN</span>').replace('<span class="key">REC AB</span>', '<span class="key">REC1</span>').replace('<span class="key">REC CD</span>', '<span class="key">REC2</span>')
    for a, b in (('◀', 'LEFT'), ('▶', 'RIGHT'), ('▲', 'UP'), ('▼', 'DOWN')):
        body = body.replace(f'<span class="key">{a}</span>', f'<span class="key">{b}</span>')
    # boxes get an inner block (the sign stands beside the text)
    body = re.sub(r'<div class="(note|tip|warn)"([^>]*)>(.*?)</div>', lambda m: f'<div class="{m.group(1)}"{m.group(2)}><div>{m.group(3)}</div></div>', body, flags=re.S)

    return body


def build(body, pages=None, markers=True):
    """pages: {marker id: page} from the pass before (None: the first pass, numbers shown as 000)."""
    used.clear()
    pg = (lambda i: str(pages.get(i, 0) or '?')) if pages is not None else (lambda i: '000')
    savers = savers_list()
    body = body.replace('{{SAVER_COUNT}}', str(len(savers)))
    body = body.replace('{{SAVERS}}', lang.S('list_sep').join(savers))
    body = macros(body)

    # a plain "chapter N" is a number of the edition before: into a reference by id, before anything else is numbered
    # ("Kapitel N", "第N章" in a translation)
    body = re.sub(lang.OLD_CHAPTER[lang.CODE], lambda m: ('{{%s:%s}}' % ('Ch' if m.group(1)[0] == 'C' else 'ch', OLD_CH[int(m.group(2))])) if int(m.group(2)) in OLD_CH else m.group(0), body)
    # chapters, numbering, markers
    mk = (lambda i: f'<span class="mk">@@{i}@@</span>') if markers else (lambda i: '')
    parts = re.split(r'(?=<h2 )', body)
    front = parts[0]; out = [front]; toc = []; names = {}; index = []
    num = 0; app = 0; auto = [0]
    def newid(prefix):
        auto[0] += 1; return f'{prefix}{auto[0]}'
    css_pages = []
    for ci, part in enumerate(parts[1:]):
        m = re.match(r'<h2 ([^>]*)>(.*?)</h2>', part, re.S)
        attrs, title = m.group(1), m.group(2).strip()
        cid = re.search(r'id="(\w+)"', attrs).group(1)
        kind = 'app' if 'class="app"' in attrs else 'plain' if 'class="plain"' in attrs else 'ch'
        if kind == 'ch':
            num += 1; label = str(num); full = f'{label}. {title}'
        elif kind == 'app':
            app += 1; label = chr(64 + app); full = lang.S('appendix_full', n=label, t=title)
        else:
            label = ''; full = title
        names[cid] = (label, title, kind)
        ptitle = re.search(r'data-part="([^"]*)"', attrs)
        if ptitle:
            toc.append(('part', ptitle.group(1), None))
        toc.append(('l1', full, cid))
        index.append((re.sub('<[^>]+>', '', title), cid))
        rest = part[m.end():]
        sub = [0, 0]
        def h3(a, t):
            sub[0] += 1; sub[1] = 0
            i = re.search(r'id="(\w+)"', a); hid = i.group(1) if i else newid('s')
            if not i:
                a += f' id="{hid}"'
            n = f'{label}.{sub[0]}' if label else ''
            toc.append(('l2', f'{n} {t}'.strip(), hid)); names[hid] = (n, t, 'sec'); index.append((re.sub('<[^>]+>', '', t), hid))
            return f'<h3{a}>{mk(hid)}<span class="num">{n}</span>{t}</h3>'
        def h4(a, t):
            sub[1] += 1
            i = re.search(r'id="(\w+)"', a); hid = i.group(1) if i else newid('s')
            if not i:
                a += f' id="{hid}"'
            n = f'{label}.{sub[0]}.{sub[1]}' if label and sub[0] else ''
            names[hid] = (n, t, 'sub'); index.append((re.sub('<[^>]+>', '', t), hid))
            return f'<h4{a}>{mk(hid)}<span class="num">{n}</span>{t}</h4>'
        rest = re.sub(r'<h([34])([^>]*)>(.*?)</h\1>', lambda mm: (h3 if mm.group(1) == '3' else h4)(mm.group(2), mm.group(3).strip()), rest, flags=re.S)
        # every knob's name in the first cell of a table row: into the index
        short = re.sub('<[^>]+>', '', title)
        def kcell(mm):
            txt = re.sub('<[^>]+>', '', mm.group(2))
            txt = re.sub(r'^(FUNC|2ND)?\s*[A-F]?\s*:\s*', '', txt).strip()
            terms = [t for t in re.split(r'[\s,/]+', txt) if re.fullmatch(r'[A-Z][A-Z0-9!]{2,5}', t)]
            if not terms or len(terms) > 3:
                return mm.group(0)
            kid = newid('k')
            for t in terms:
                index.append((t, kid))
            return f'<td class="k"{mm.group(1)}>{mk(kid)}{mm.group(2)}</td>'
        rest = re.sub(r'<td class="k"([^>]*)>(.*?)</td>', kcell, rest, flags=re.S)
        def ix(mm):
            kid = newid('x'); index.append((mm.group(1), kid)); return mk(kid)
        rest = re.sub(r'\{\{ix:([^}]+)\}\}', ix, rest)
        head = f'{label}. {short}' if kind == 'ch' else lang.S('appendix_full', n=label, t=short) if kind == 'app' else short
        head = head.upper().replace('\\', '').replace('"', "'")
        css_pages.append(f'@page c{ci}:left {{ @top-left {{ content: "{head}"; }} @top-right {{ content: ""; }} }} @page c{ci}:right {{ @top-right {{ content: "{head}"; }} @top-left {{ content: ""; }} }}')
        numspan = f'<span class="num">{label}.</span>' if kind == 'ch' else f'<span class="num">{lang.S("appendix_num", n=label)}</span>' if kind == 'app' else ''
        out.append(f'<section class="chapter" style="page: c{ci}"><h2 {attrs}>{mk(cid)}{numspan}{title}</h2>{rest}</section>')
    body = ''.join(out)
    # an id that {{pg:}} points at on something other than a heading (a list item, a box): a marker there too
    other = set()
    for i in set(re.findall(r'\{\{pg:(\w+)\}\}', body)) - set(names):
        body, k = re.subn(r'(<[a-z][a-z0-9]*\b[^>]*\bid="' + i + r'"[^>]*>)', lambda mm: mm.group(1) + mk(i), body, count=1)
        if k:
            other.add(i)

    # cross-references
    def ch_ref(cid, with_page=True, cap=False):
        if cid not in names:
            raise SystemExit(f'{lang.NAMES[lang.CODE]} ({os.path.basename(SRC_NAME[0])}): a reference to an id the text does not have: {cid}')
        label, title, kind = names[cid]
        word = {'ch': lang.S('ref_ch', n=label), 'app': lang.S('ref_app', n=label), 'plain': title, 'sec': lang.S('ref_sec', n=label), 'sub': lang.S('ref_sec', n=label)}[kind]
        if kind in ('sec', 'sub') and not label:
            word = lang.S('ref_quote', t=title)
        p = f'<span class="pg">{lang.S("ref_page_after", p=pg(cid))}</span>' if with_page else ''
        if cap:
            word = word[0].upper() + word[1:]
        return f'<a class="xref" href="#{cid}">{word}{p}</a>'
    body = re.sub(r'\{\{ch:(\w+)\}\}', lambda m: ch_ref(m.group(1)), body)
    body = re.sub(r'\{\{Ch:(\w+)\}\}', lambda m: ch_ref(m.group(1), cap=True), body)
    body = re.sub(r'\{\{chn:(\w+)\}\}', lambda m: ch_ref(m.group(1), False), body)
    def pg_ref(m):
        if m.group(1) not in names and m.group(1) not in other:
            raise SystemExit(f'{lang.NAMES[lang.CODE]} ({os.path.basename(SRC_NAME[0])}): {{{{pg:{m.group(1)}}}}} points at an id the text does not have')
        return f'<a class="xref" href="#{m.group(1)}">{lang.S("ref_page", p=pg(m.group(1)))}</a>'
    body = re.sub(r'\{\{pg:(\w+)\}\}', pg_ref, body)

    def title_ref(m):
        if m.group(1) not in names:
            raise SystemExit(f'{lang.NAMES[lang.CODE]} ({os.path.basename(SRC_NAME[0])}): {{{{title:{m.group(1)}}}}} points at an id the text does not have')
        return names[m.group(1)][1]
    body = re.sub(r'\{\{title:(\w+)\}\}', title_ref, body)
    # the table of contents
    t = ''
    for cls, text, cid in toc:
        if cls == 'part':
            t += f'<div class="part">{text}</div>'
        else:
            t += f'<div class="{cls}"><a href="#{cid}"><span>{text}</span><span class="dots"></span><span class="n">{pg(cid)}</span></a></div>'
    body = body.replace('{{TOC}}', f'<div class="toc">{t}</div>')
    # the index
    ent = {}
    for term, cid in index:
        term = re.sub(r'\s+', ' ', html.unescape(term)).strip()
        term = re.sub(lang.ARTICLES[lang.CODE], '', term)
        if len(term) < 2 and not lang.cjk():
            continue
        key = term if term.isupper() else term[0].upper() + term[1:]
        ent.setdefault(key, [])
        ent[key].append(cid)
    ix = ''; letter = ''
    for term in sorted(ent, key=lang.index_key):
        first = lang.index_group(term)
        if first != letter:
            letter = first; ix += f'<div class="il">{letter}</div>'
        seen = []; links = []
        for cid in ent[term]:
            p = pg(cid)
            if p not in seen:
                seen.append(p); links.append(f'<a href="#{cid}">{p}</a>')
        ix += f'<div class="ie">{esc(term)} <span class="pn">{", ".join(links[:8])}</span></div>'
    body = body.replace('{{INDEX}}', f'<div class="index">{ix}</div>')
    body = body.replace('{{BUILD}}', lang.S('build_name'))
    mbox = margin_box_css()
    head_css = ('@page { @top-left { ' + mbox + ' } @top-right { ' + mbox + ' } }\n'
                f'@page front:left {{ @top-left {{ content: "{lang.S("head_manual")}"; }} }} @page front:right {{ @top-right {{ content: "{lang.S("head_toc")}"; }} }}\n' + '\n'.join(css_pages))
    doc = f'<!doctype html>\n<html lang="{lang.HTML_LANG[lang.CODE]}">\n<head>\n<meta charset="utf-8">\n<title>{lang.S("title_manual")}</title>\n<style>{stylesheet()}\n{head_css}\n@page cover {{ margin: 0; @top-left {{ content: none; border: none; }} @top-right {{ content: none; border: none; }} @bottom-center {{ content: none; border: none; }} }}\n</style>\n</head>\n<body>\n{body}\n</body>\n</html>\n'
    return doc, names


def to_pdf(html_path, pdf_path):
    """Chrome prints the page. Each print has a profile of its own (so that several builds can run at once), and
    one that fails is tried again."""
    for attempt in range(3):
        with tempfile.TemporaryDirectory(prefix='aeria_chrome_') as prof:
            r = subprocess.run([CHROME, '--headless', '--disable-gpu', '--no-pdf-header-footer', '--run-all-compositor-stages-before-draw', f'--user-data-dir={prof}',
                                '--virtual-time-budget=20000', f'--print-to-pdf={pdf_path}', 'file://' + html_path], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if r.returncode == 0 and os.path.exists(pdf_path):
            return
    raise SystemExit(f'Chrome could not print {html_path} (exit {r.returncode})')


def marker_pages(pdf_path):
    txt = subprocess.run(['pdftotext', '-raw', pdf_path, '-'], check=True, capture_output=True).stdout.decode('utf-8', 'replace')
    pages = {}
    for n, page in enumerate(txt.split('\f')):
        for i in re.findall(r'@@(\w+)@@', page):
            pages.setdefault(i, n + 1)
    return pages, txt.count('\f')


SRC_NAME = ['body.html']      # the file being built, for the messages


def compare_with_english(text, english, name):
    """A translation should have the ids, the references and the macros of the English. What differs is printed,
    with the language's name, before the build (a reference to a missing id stops the build later)."""
    if text is english:
        return []
    def ids(t):
        return re.findall(r'\bid="(\w+)"', t)
    def refs(t):
        return re.findall(r'\{\{(?:ch|Ch|chn|pg|title|man|ix):[^}]*\}\}', t)
    def macros_of(t):
        return re.findall(r'\[\[(?:S|G\d|/G|ANNO|PANELW?|LAMPS|TRIGS|STEP|/STEP)\b|\{\{[A-Z_]+(?: \w+)?\}\}', t)
    out = []
    lname = lang.NAMES[lang.CODE]
    a, b = set(ids(english)), set(ids(text))
    if a - b:
        out.append(f'{lname} ({name}): ids of the English missing here: {sorted(a - b)}')
    if b - a:
        out.append(f'{lname} ({name}): ids not in the English: {sorted(b - a)}')
    from collections import Counter
    ra, rb = Counter(r for r in refs(english) if not r.startswith('{{ix:')), Counter(r for r in refs(text) if not r.startswith('{{ix:'))
    if ra != rb:
        out.append(f'{lname} ({name}): cross-references differ from the English: missing {sorted((ra - rb).elements())}, extra {sorted((rb - ra).elements())}')
    ma, mb = Counter(macros_of(english)), Counter(macros_of(text))
    if ma != mb:
        out.append(f'{lname} ({name}): macros differ from the English: missing {sorted((ma - mb).elements())}, extra {sorted((mb - ma).elements())}')
    return out


def pdf_pages(pdf):
    info = subprocess.run(['pdfinfo', pdf], capture_output=True).stdout.decode()
    fp = re.search(r'Pages:\s+(\d+)', info)
    return int(fp.group(1)) if fp else None


def main():
    code = arg('--lang', 'en'); theme_name = arg('--theme', 'dark')
    set_theme(theme_name); set_lang(code)
    body_path = arg('--body', source('body', code))
    if not os.path.exists(body_path):
        raise SystemExit(f'{lang.NAMES[code]}: there is no {os.path.basename(body_path)} yet')
    SRC_NAME[0] = body_path
    body = open(body_path).read()
    for msg in compare_with_english(body, open(source('body', 'en')).read() if code != 'en' else body, os.path.basename(body_path)):
        print('CHECK', msg)
    test = '--test' in sys.argv            # a trial run: _test_manual_<lang>_<theme>.html and .pdf, the real ones left alone
    out, pdf, tmp_html, tmp_pdf, extra_pdf, extra_html = out_paths(code, theme_name, test)
    print(f'{lang.NAMES[code]}, {theme_name}: {os.path.basename(body_path)} -> {pdf}')
    # every screensaver the firmware names should be in the chapter
    lacking = [n for n in savers_list() if ('ss_' + n.lower()) not in body and ('<b>' + n + '</b>') not in body and ('<h3>' + n + '</h3>') not in body]
    if lacking:
        print('screensavers in screensavers.c with no entry in the chapter:', lacking)
    if '--pdf' not in sys.argv:
        doc, _ = build(body, None, markers=False)
        open(out, 'w').write(doc)
        print(len(doc), 'bytes;', len(used), 'pictures placed;', len(set(used)), 'distinct (no page numbers: run with --pdf)')
    else:
        pages = None
        for n in range(4):
            doc, names = build(body, pages, markers=True)
            open(tmp_html, 'w').write(doc)
            to_pdf(tmp_html, tmp_pdf)
            new, count = marker_pages(tmp_pdf)
            print(f'pass {n + 1}: {count} pages, {len(new)} markers')
            if new == pages:
                break
            pages = new
        # the pages the body counts from: the printed number is the PDF's page
        doc, names = build(body, pages, markers=False)
        open(out, 'w').write(doc)
        to_pdf(out, pdf)
        final = pdf_pages(pdf)
        print('final:', final or '?', 'pages (marker pass:', count, ')')
        for f in extra_pdf:
            shutil.copyfile(pdf, f); print('also', f)
        for f in extra_html:
            open(f, 'w').write(doc)
        os.remove(tmp_html); os.remove(tmp_pdf)
        missing = [i for i in names if i not in pages]
        if missing:
            print(f'CHECK {lang.NAMES[code]} ({os.path.basename(body_path)}): ids with no page found:', missing[:20])
    if not test and code == 'en':
        panel.export(img_dir, CHROME)          # the panel drawings as files too: img/panel_*.svg and .png (light: panel_*_light.*)
    have = {f[:-4] for f in os.listdir(img_dir) if f.endswith('.png') and not f.startswith('panel_')}
    print(len(used), 'pictures placed;', len(set(used)), 'distinct')
    print('not placed:', sorted(have - set(used) - {'aeria_logo'}))


if __name__ == '__main__':
    main()
