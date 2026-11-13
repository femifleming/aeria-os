"""The colours of the AERIA OS documents (the manual and the quick guide): a dark theme and a light theme, defined here once.

build_manual.py and build_quick.py write these into the stylesheet as CSS variables on :root (see css_root), and
panel.py and diagrams.py draw with the same values, so a drawing and the page it stands on always agree.
Change a colour here and rebuild; nothing else holds a colour of the page.

A theme is picked by name with use('dark') or use('light') before panel.py and diagrams.py are (re)loaded: the
builders do this (build_manual.set_theme). The module's names (BG, INK, ...) are always those of the theme in use;
'dark' is the one in use when nothing has been picked.

Dark, measured against BG (#15171c): INK 14:1, SOFT 8:1, ACC 8:1, BLUE 8:1.
Light, measured against BG (#ffffff): INK 17.8:1, SOFT 7.5:1, ACC 5.2:1 (bold labels and drawings only), BLUE 8.1:1.
In both, the unit's screen pictures stay white on black (SCREEN), framed with SCREEN_EDGE."""

THEMES = {
    'dark': dict(
        BG='#15171c',            # the page, edge to edge
        INK='#e4e6eb',           # running text, rules under headings, call-out lines
        BRIGHT='#f6f7f9',        # the strongest text: bold words, lettering on keys
        SOFT='#a9aeb9',          # captions, page references, small print
        FAINT='#6f7582',         # dotted leaders, dashed outlines
        RULE='#383c46',          # hairlines in tables and between rows
        TINT='#20242c',          # a box's ground: notes, row headings, bands in a diagram
        ZEBRA='#1a1d23',         # every second table row
        HEAD='#2e333e',          # a table's heading row
        HEAD_INK='#f6f7f9',      # the lettering of a table's heading row
        ACC='#ff8f45',           # the accent: orange
        ACC_GROUND='#33200f',    # a warning's ground, an orange box in a diagram
        BLUE='#82b4e8',          # the second accent
        BLUE_GROUND='#14263a',
        ON_ACC='#15171c',        # lettering on the accent
        KEY_BG='#2b2f38',        # a key-cap in the text
        KEY_EDGE='#7b818d',
        SCREEN='#000000',        # the unit's screen: black, as it is
        SCREEN_EDGE='#5a606c',   # the frame round a screen picture
        LOGO_FILTER='invert(0.92)',   # the cover's logo is drawn black on white: turned round for the dark page
        # the diagrams (diagrams.py)
        D_SLAB='#3d4451', D_SLAB_EDGE='#aeb4c0', D_SLAB_TXT='#f6f7f9',   # a "dark" box: the one that stands out
        D_TINT_BOX='#2a2f39',                                             # a "tint" box
        D_GREY='#7c8087',                                                 # the other steps of the WARP drawing
        # the panel drawing (panel.py)
        P_PANEL='#2a2d35', P_PANEL_EDGE='#59606c',      # the panel's face and its outline
        P_KEY='#59606c', P_KEY_TXT='#f6f7f9',           # a key, its lettering
        P_LEGEND='#c3c7cf',                             # what is printed on the panel
        P_LED='#454a55',
        P_DIM_PANEL='#1c1f25', P_DIM_EDGE='#3a3f49',    # the panel where only some controls are lit
        P_DIM_KEY='#333842', P_DIM_TXT='#737987',
        P_DIM_LEGEND='#4a4f5a',
        P_HI='#f6f7f9', P_HI_TXT='#15171c',             # a lit control: white, ringed in the accent
        P_NIGHT='#0d0e11',                              # the panel in the dark (the lamp drawings)
        P_DEEP='#0d0e11',                               # holes: the fader's slot, the screen's bezel, sockets
        P_GLASS='#000000', P_GLASS_DIM='#15171c',       # the screen's glass, lit and dimmed
        P_REAR_METAL='#7b818d', P_REAR_MID='#454a55', P_REAR_HOLE='#1c1f25', P_REAR_TXT='#ffffff',
    ),
    'light': dict(
        BG='#ffffff',
        INK='#16181d',
        BRIGHT='#0b0c0f',
        SOFT='#50555f',
        FAINT='#8d9096',
        RULE='#d5d8de',
        TINT='#f1f2f5',
        ZEBRA='#f7f8fa',
        HEAD='#16181d',
        HEAD_INK='#ffffff',
        ACC='#c2410c',
        ACC_GROUND='#fdf1ec',
        BLUE='#27527d',
        BLUE_GROUND='#eaf1f8',
        ON_ACC='#ffffff',
        KEY_BG='#fafbfc',
        KEY_EDGE='#70757e',
        SCREEN='#000000',
        SCREEN_EDGE='#16181d',
        LOGO_FILTER='none',
        D_SLAB='#16181d', D_SLAB_EDGE='#16181d', D_SLAB_TXT='#ffffff',
        D_TINT_BOX='#f1f2f5',
        D_GREY='#9aa0ab',
        P_PANEL='#c9cbce', P_PANEL_EDGE='#9b9ea3',
        P_KEY='#7c8087', P_KEY_TXT='#ffffff',
        P_LEGEND='#34373d',
        P_LED='#8b8f95',
        P_DIM_PANEL='#e6e7ea', P_DIM_EDGE='#b9bcc1',
        P_DIM_KEY='#c8cace', P_DIM_TXT='#f4f4f5',
        P_DIM_LEGEND='#a9acb2',
        P_HI='#16181d', P_HI_TXT='#ffffff',
        P_NIGHT='#24262b',
        P_DEEP='#3b3e44',
        P_GLASS='#000000', P_GLASS_DIM='#9a9da3',
        P_REAR_METAL='#5d6066', P_REAR_MID='#3b3e44', P_REAR_HOLE='#2c2f34', P_REAR_TXT='#16181d',
    ),
}

NAME = None
VARS = {}


def use(name):
    """Make `name` ('dark' or 'light') the theme in use: sets this module's names and VARS."""
    global NAME, VARS
    if name not in THEMES:
        raise SystemExit(f'no theme called {name!r}: {", ".join(THEMES)}')
    NAME = name
    t = THEMES[name]
    globals().update(t)
    g = globals()
    g['ON_INK'] = t['BG']
    g['P_HI_RING'] = t['ACC']
    VARS = {'bg': BG, 'ink': INK, 'bright': BRIGHT, 'soft': SOFT, 'faint': FAINT, 'rule': RULE, 'tint': TINT, 'zebra': ZEBRA, 'head': HEAD,
            'head-ink': HEAD_INK, 'accent': ACC, 'accent-ground': ACC_GROUND, 'blue': BLUE, 'blue-ground': BLUE_GROUND, 'on-accent': ON_ACC,
            'key-bg': KEY_BG, 'key-edge': KEY_EDGE, 'screen': SCREEN, 'screen-edge': SCREEN_EDGE, 'logo-filter': LOGO_FILTER}


def css_root():
    """:root { --bg: ...; --ink: ...; } for the stylesheet."""
    return ':root { ' + ' '.join(f'--{k}: {v};' for k, v in VARS.items()) + ' }'


def fill(css):
    """@page rules cannot read the variables of :root, so the stylesheet writes %INK%, %BG% ... there."""
    for k, v in VARS.items():
        css = css.replace('%' + k.upper().replace('-', '_') + '%', v)
    return css


def contrast(a, b):
    """WCAG contrast ratio of two #rrggbb colours."""
    def lum(c):
        c = c.lstrip('#'); rgb = [int(c[i:i + 2], 16) / 255 for i in (0, 2, 4)]
        rgb = [v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4 for v in rgb]
        return 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2]
    la, lb = sorted((lum(a), lum(b)), reverse=True)
    return (la + 0.05) / (lb + 0.05)


use('dark')

if __name__ == '__main__':
    for n in THEMES:
        use(n)
        print(n, ' '.join(f'{k}:{contrast(globals()[k], BG):.1f}' for k in ('INK', 'BRIGHT', 'SOFT', 'ACC', 'BLUE')),
              f'INK/TINT:{contrast(INK, TINT):.1f} INK/ZEBRA:{contrast(INK, ZEBRA):.1f} INK/ACC_GROUND:{contrast(INK, ACC_GROUND):.1f}',
              f'HEAD_INK/HEAD:{contrast(HEAD_INK, HEAD):.1f} BRIGHT/KEY_BG:{contrast(BRIGHT, KEY_BG):.1f} SOFT/ZEBRA:{contrast(SOFT, ZEBRA):.1f}',
              f'BLUE/TINT:{contrast(BLUE, TINT):.1f} ACC/ZEBRA:{contrast(ACC, ZEBRA):.1f} P_LEGEND/P_PANEL:{contrast(P_LEGEND, P_PANEL):.1f} ON_ACC/ACC:{contrast(ON_ACC, ACC):.1f}')
