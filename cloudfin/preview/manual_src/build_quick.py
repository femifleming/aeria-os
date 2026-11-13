#!/usr/bin/env python3
"""Builds the AERIA OS Quick Guide: quick.html (this folder) -> aeria-os/manual/aeria_os_quick_guide.html and, with
--pdf, AERIA_OS_Quick_Guide.pdf through headless Chrome.

It is the manual's little brother and is made from the same parts: build_manual.py's stylesheet and macros
([[S]] [[G3]] [[ANNO]] [[PANEL]] [[STEP]] [[TRIGS]] [[LAMPS]] {{FRONT}} {{REAR}} {{DIAGRAM name}} [KEY] ((KNOB)) ``MSG``),
panel.py, diagrams.py, the screen pictures in aeria-os/manual/img and the colours of theme.py. Only what is special to
the quick guide is here: the cover, the numbering of its sections, its contents list and a few more styles.

In quick.html
  <h2 id="x">Title</h2>            a section: numbered, starts a new page (class="run": carries on down the same page)
  <h3>Title</h3>                   a sub-section, numbered 4.2
  {{QTOC}}                         the contents, with page numbers
  {{man:id}}                       a pointer into the full manual: the title of the chapter or section of body.html
                                   with that id, read from body.html at build time (so it follows a retitled chapter)
  <div class="cards"> <div class="card"><div class="ct">Title</div> <div class="cr"><span>keys</span><span>what</span></div> ...
                                   the key-combination cards
  {{SAVER_COUNT}}                  the number of screensavers, read from the firmware

Themes and languages as in build_manual.py: --theme dark|light, --lang en|zh|ja|de (quick.<lang>.html; its {{man:}}
pointers read the titles of body.<lang>.html). Output: aeria-os/manual/<lang>/AERIA_OS_Quick_Guide_<Dark|Light>.pdf,
and for English in the dark theme also aeria-os/manual/AERIA_OS_Quick_Guide.pdf. The last section, the key sheet
(id qkeys), must stay on one page: the build says so when it does not, and by how much.

Usage: build_quick.py [--pdf] [--test] [--lang en|zh|ja|de] [--theme dark|light]"""
import re, os, sys, subprocess, shutil
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_manual as bm
import theme, lang

here = os.path.dirname(os.path.abspath(__file__))
man_dir = bm.man_dir

QCSS = r"""
/* the quick guide's own styles, on top of the manual's */
@page { @top-left { content: "AERIA OS"; } @top-right { content: "%HEAD_QUICK%"; } }
h2 { margin-bottom: 10px; }
h2.run { margin-top: 26px; }
h3 { margin-top: 18px; }
.cover .sub { letter-spacing: 0.3em; }
.cover .big { font-size: 15pt; letter-spacing: 0.42em; color: var(--accent); font-weight: 700; margin-top: 30px; }
.lead { font-size: 11.6pt; line-height: 1.5; color: var(--ink); }
.two { display: grid; grid-template-columns: 1fr 1fr; gap: 6px 22px; align-items: start; }
.two > * { min-width: 0; }
.two.w64 { grid-template-columns: 62fr 38fr; } .two.w46 { grid-template-columns: 42fr 58fr; }
.manref { display: block; margin: 9px 0 4px; font-size: 8.8pt; color: var(--soft); }
.manref::before { content: "%IN_MANUAL%"; font-weight: 700; font-size: 7.4pt; letter-spacing: 0.1em; color: var(--accent); border: 0.8px solid var(--accent); border-radius: 3px; padding: 0.5px 5px; margin-right: 7px; }
.manref b { color: var(--ink); }
.qtoc { margin: 6px 0 4px; }
.qtoc a { display: flex; align-items: baseline; font-size: 9.2pt; padding: 1.6px 0; break-inside: avoid; }
.qtoc .tn { flex: 0 0 22px; font-weight: 700; color: var(--accent); }
.qtoc .dots { flex: 1; border-bottom: 1.2px dotted var(--faint); margin: 0 5px; transform: translateY(-3px); }
.qtoc .n { font-variant-numeric: tabular-nums; }
table.tight { font-size: 8.7pt; line-height: 1.36; } table.tight th, table.tight td { padding: 3.6px 7px; }
table.plist td:first-child { width: 24px; font-weight: 700; color: var(--accent); text-align: right; padding-right: 2px; }
table.plist td:nth-child(2) { width: 19%; }
figure.one { width: 50%; }
figure.half { width: 100%; margin: 6px 0; }
.two figure.one { width: 100%; margin: 4px 0 8px; }
.two .shots.c2 { padding: 0; }
figure.draw.narrow { margin-left: 9%; margin-right: 9%; }
.step .st p { margin: 3px 0; }
.step { margin: 7px 0; padding-bottom: 7px; }
/* key-combination cards */
.cards { columns: 3; column-gap: 9px; margin: 10px 0 4px; }
.cards .card { margin: 0 0 9px; }
.cards.c2 { columns: 2; }
.card { border: 0.8px solid var(--rule); border-radius: 6px; background: var(--zebra); padding: 7px 9px 5px; break-inside: avoid; }
.card .ct { font-weight: 700; font-size: 8.2pt; letter-spacing: 0.08em; text-transform: uppercase; color: var(--accent); margin-bottom: 4px; }
.card .cr { display: flex; gap: 8px; align-items: baseline; padding: 2.6px 0; border-top: 0.6px solid var(--rule); font-size: 8.3pt; line-height: 1.34; }
.card .cr > span:first-child { flex: 0 0 47%; line-height: 1.7; }
.card .cr > span:last-child { flex: 1; }
.cards.sheet { margin: 6px 0; } .cards.sheet .card { margin: 0 0 7px; padding: 5px 8px 3px; }
.cards.sheet .card .cr { font-size: 7.25pt; padding: 0.9px 0; line-height: 1.3; } .cards.sheet .card .cr > span:first-child { flex-basis: 45%; line-height: 1.5; }
.cards.sheet .card .ct { margin-bottom: 2px; } .cards.sheet .card { margin-bottom: 6px; }
p.more { font-size: 8.6pt; color: var(--soft); border-top: 2px solid var(--accent); padding-top: 6px; margin-top: 4px; }
.cards.sheet .key { font-size: 0.78em; padding: 0 3.5px; }
.big3 { display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 10px; margin: 10px 0; }
.big3 > div { border-top: 2px solid var(--accent); padding-top: 6px; font-size: 9.3pt; line-height: 1.42; }
.big3 > div b:first-child { display: block; text-transform: uppercase; letter-spacing: 0.05em; font-size: 9pt; margin-bottom: 2px; }
"""


def manual_titles():
    """id -> title, for every chapter and section of the full manual that has an id: of the manual in the same
    language (body.<lang>.html), or of the English one while there is no translation."""
    path = bm.source('body', lang.CODE)
    if not os.path.exists(path):
        print(f'CHECK {lang.NAMES[lang.CODE]}: no {os.path.basename(path)} yet: the pointers into the manual name its English titles')
        path = bm.source('body', 'en')
    body = open(path).read()
    return {m.group(2): re.sub(r'<[^>]+>', '', m.group(3)).strip() for m in re.finditer(r'<h([234])[^>]*\bid="(\w+)"[^>]*>(.*?)</h\1>', body, re.S)}


def link_sections(body, link):
    """Applies lang.QUICK_SECTION to the text between the tags, outside links and the style."""
    parts = re.split(r'(<[^>]+>)', body); inside = 0
    for k, p in enumerate(parts):
        if p.startswith('<'):
            if re.match(r'<(a|style|svg)\b', p): inside += 1
            elif re.match(r'</(a|style|svg)>', p): inside -= 1
        elif not inside and p:
            parts[k] = re.sub(lang.QUICK_SECTION[lang.CODE], link, p)
    return ''.join(parts)


def build(src, pages=None, markers=False):
    pg = (lambda i: str(pages.get(i, '?'))) if pages is not None else (lambda i: '00')
    titles = manual_titles()

    def man(m):
        ids = [i for i in m.group(1).split(',') if i.strip()]
        for i in ids:
            if i.strip() not in titles:
                raise SystemExit(f'{lang.NAMES[lang.CODE]} ({os.path.basename(bm.source("quick", lang.CODE))}): {{{{man:}}}} points at a place the manual does not have: ' + i)
        return '<span class="manref">' + ' &nbsp;·&nbsp; '.join(f'<b>{titles[i.strip()]}</b>' for i in ids) + '</span>'
    src = re.sub(r'\{\{man:([\w, ]+)\}\}', man, src)
    def title(m):
        if m.group(1) not in titles:
            raise SystemExit(f'{lang.NAMES[lang.CODE]} ({os.path.basename(bm.source("quick", lang.CODE))}): {{{{title:{m.group(1)}}}}} points at a place the manual does not have')
        return titles[m.group(1)]
    src = re.sub(r'\{\{title:(\w+)\}\}', title, src)
    src = src.replace('{{SAVER_COUNT}}', str(len(bm.savers_list()))).replace('{{BUILD}}', lang.S('build_name'))
    body = bm.macros(src)
    mk = (lambda i: f'<span class="mk">@@{i}@@</span>') if markers else (lambda i: '')
    parts = re.split(r'(?=<h2[ >])', body)
    out = [parts[0]]; toc = []
    for n, part in enumerate(parts[1:], 1):
        m = re.match(r'<h2([^>]*)>(.*?)</h2>', part, re.S)
        attrs, title = m.group(1), m.group(2).strip()
        cid = re.search(r'id="(\w+)"', attrs).group(1)
        run = 'class="run"' in attrs
        toc.append((n, title, cid))
        sub = [0]

        def h3(mm):
            sub[0] += 1
            return f'<h3{mm.group(1)}><span class="num">{n}.{sub[0]}</span>{mm.group(2).strip()}</h3>'
        rest = re.sub(r'<h3([^>]*)>(.*?)</h3>', h3, part[m.end():], flags=re.S)
        out.append(f'<section class="{"" if run else "chapter"}"><h2{attrs}>{mk(cid)}<span class="num">{n}.</span>{title}</h2>{rest}</section>')
    body = ''.join(out)
    # "section 10" (Abschnitt 10, ...) in the text: a link to that section of this guide
    def sec_link(mm):
        k = int(mm.group(2))
        if 1 <= k <= len(toc):
            return f'<a class="xref" href="#{toc[k - 1][2]}">{mm.group(1)}</a>'
        print(f'CHECK {lang.NAMES[lang.CODE]}: "{mm.group(1)}": the guide has no section {k}')
        return mm.group(0)
    body = link_sections(body, sec_link)
    t = ''.join(f'<a href="#{cid}"><span class="tn">{n}</span><span>{re.sub("<[^>]+>", "", title)}</span><span class="dots"></span><span class="n">{pg(cid)}</span></a>' for n, title, cid in toc)
    body = body.replace('{{QTOC}}', f'<div class="qtoc">{t}</div>')
    mbox = bm.margin_box_css()
    head_css = '@page { @top-left { ' + mbox + ' } @top-right { ' + mbox + ' } }'
    qcss = QCSS.replace('%HEAD_QUICK%', lang.S('head_quick')).replace('%IN_MANUAL%', lang.S('in_manual'))
    css = bm.stylesheet()
    cover_css = '@page cover { margin: 0; @top-left { content: none; border: none; } @top-right { content: none; border: none; } @bottom-center { content: none; border: none; } }'
    return (f'<!doctype html>\n<html lang="{lang.HTML_LANG[lang.CODE]}">\n<head>\n<meta charset="utf-8">\n<title>{lang.S("title_quick")}</title>\n<style>{css}\n{head_css}\n{qcss}\n{lang.css()}\n{cover_css}\n</style>\n</head>\n'
            f'<body>\n{body}\n</body>\n</html>\n'), [c for _, _, c in toc]


def keysheet_check(pdf, pages, ids):
    """The key sheet (the last section, qkeys) must be one page. Returns a message when it is not, with how far it runs
    over: the pages after its first, and how far down the last of them the text goes."""
    if 'qkeys' not in pages:
        return f'CHECK {lang.NAMES[lang.CODE]}: the key sheet (id qkeys) was not found'
    n = bm.pdf_pages(pdf); first = pages['qkeys']
    if ids and ids[-1] != 'qkeys' or n == first:
        return None
    # how far down the last page the overflow reaches (pdftotext gives the words' boxes in points)
    xml = subprocess.run(['pdftotext', '-bbox', '-f', str(n), '-l', str(n), pdf, '-'], capture_output=True).stdout.decode('utf-8', 'replace')
    pt = 72 / 25.4
    ys = [float(y) for y in re.findall(r'<word [^>]*yMax="([\d.]+)"', xml) if float(y) < (297 - 22) * pt]   # not the page number
    words = len(ys)
    mm = (max(ys) - 24 * pt) / pt if ys else 0
    return (f'CHECK {lang.NAMES[lang.CODE]}: the key sheet does not fit one page: it runs from page {first} to page {n} '
            f'({n - first} page(s) more; on page {n}: {words} words, {mm:.0f} mm of the 251 mm text area)')


def main():
    code = bm.arg('--lang', 'en'); theme_name = bm.arg('--theme', 'dark')
    bm.set_theme(theme_name); bm.set_lang(code)
    path = bm.source('quick', code)
    if not os.path.exists(path):
        raise SystemExit(f'{lang.NAMES[code]}: there is no {os.path.basename(path)} yet')
    src = open(path).read()
    for msg in bm.compare_with_english(src, open(bm.source('quick', 'en')).read() if code != 'en' else src, os.path.basename(path)):
        print('CHECK', msg)
    test = '--test' in sys.argv
    out, pdf, tmp_html, tmp_pdf, extra_pdf, extra_html = bm.out_paths(code, theme_name, test, kind='quick')
    print(f'{lang.NAMES[code]}, {theme_name}: {os.path.basename(path)} -> {pdf}')
    if '--pdf' not in sys.argv:
        doc, _ = build(src)
        open(out, 'w').write(doc)
        print(len(doc), 'bytes (no page numbers: run with --pdf)')
        return
    pages = None
    for n in range(3):
        doc, ids = build(src, pages, markers=True)
        open(tmp_html, 'w').write(doc)
        bm.to_pdf(tmp_html, tmp_pdf)
        new, count = bm.marker_pages(tmp_pdf)
        if new == pages:
            break
        pages = new
    doc, ids = build(src, pages, markers=False)
    open(out, 'w').write(doc)
    bm.to_pdf(out, pdf)
    print(pdf + ':', bm.pdf_pages(pdf) or '?', 'pages;', len(set(bm.used)), 'screen pictures')
    for f in extra_pdf:
        shutil.copyfile(pdf, f); print('also', f)
    for f in extra_html:
        open(f, 'w').write(doc)
    os.remove(tmp_html); os.remove(tmp_pdf)
    missing = [i for i in ids if i not in pages]
    if missing:
        print(f'CHECK {lang.NAMES[code]} ({os.path.basename(path)}): sections with no page found:', missing)
    k = keysheet_check(pdf, pages, ids)
    if k:
        print(k)


if __name__ == '__main__':
    main()
