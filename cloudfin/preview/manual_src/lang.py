"""The languages of the AERIA OS documents: English (en), Simplified Chinese (zh), Japanese (ja) and German (de).

The text of the manual and the quick guide is in body.html / quick.html (English) and body.<lang>.html /
quick.<lang>.html (the translations). What the builders write themselves (running heads, the words of the
cross-references, the index's grouping, the drawings' lettering ...) comes from here: STRINGS for the words of
the builders, DIAGRAMS for the lettering of diagrams.py (keyed by the English, which is used where a translation
is missing). The names printed on the unit (keys, knobs, sockets) and the screen's messages stay in English.

use(code) picks the language; S(key, **values) gives a builder's string; tr(english) a diagram's."""
import re, unicodedata

LANGS = ('en', 'zh', 'ja', 'de')
NAMES = {'en': 'English', 'zh': 'Simplified Chinese', 'ja': 'Japanese', 'de': 'German'}
CODE = 'en'

HTML_LANG = {'en': 'en', 'zh': 'zh-Hans', 'ja': 'ja', 'de': 'de'}

# the type: Helvetica Neue for the Latin letters everywhere (the key-caps and the panel are lettered in it), then a
# font with the CJK characters, so that Chrome embeds it for them
LATIN = '"Helvetica Neue", Helvetica, Arial'
FONTS = {'en': LATIN + ', sans-serif',
         'de': LATIN + ', sans-serif',
         'zh': LATIN + ', "PingFang SC", "Hiragino Sans GB", "Heiti SC", sans-serif',
         'ja': LATIN + ', "Hiragino Sans", "Hiragino Kaku Gothic ProN", "Hiragino Kaku Gothic Pro", sans-serif'}

STRINGS = {
    # <title> of the documents
    'title_manual': {'en': 'AERIA OS User Manual', 'zh': 'AERIA OS 用户手册', 'ja': 'AERIA OS ユーザーマニュアル', 'de': 'AERIA OS Benutzerhandbuch'},
    'title_quick': {'en': 'AERIA OS Quick Guide', 'zh': 'AERIA OS 快速入门', 'ja': 'AERIA OS クイックガイド', 'de': 'AERIA OS Kurzanleitung'},
    # the running heads
    'head_manual': {'en': 'AERIA OS USER MANUAL', 'zh': 'AERIA OS 用户手册', 'ja': 'AERIA OS ユーザーマニュアル', 'de': 'AERIA OS BENUTZERHANDBUCH'},
    'head_toc': {'en': 'TABLE OF CONTENTS', 'zh': '目录', 'ja': '目次', 'de': 'INHALTSVERZEICHNIS'},
    'head_quick': {'en': 'QUICK GUIDE', 'zh': '快速入门', 'ja': 'クイックガイド', 'de': 'KURZANLEITUNG'},
    # the quick guide's pointer into the manual
    'in_manual': {'en': 'IN THE MANUAL', 'zh': '详见手册', 'ja': 'マニュアル参照', 'de': 'IM HANDBUCH'},
    # an appendix in its heading, the contents and the running head
    'appendix_num': {'en': 'Appendix {n}:', 'zh': '附录 {n}：', 'ja': '付録 {n}：', 'de': 'Anhang {n}:'},
    'appendix_full': {'en': 'Appendix {n}: {t}', 'zh': '附录 {n}：{t}', 'ja': '付録 {n}：{t}', 'de': 'Anhang {n}: {t}'},
    # {{ch:id}} {{pg:id}}: the words of a cross-reference
    'ref_ch': {'en': 'chapter {n}', 'zh': '第{n}章', 'ja': '第{n}章', 'de': 'Kapitel {n}'},
    'ref_app': {'en': 'appendix {n}', 'zh': '附录 {n}', 'ja': '付録 {n}', 'de': 'Anhang {n}'},
    'ref_sec': {'en': 'section {n}', 'zh': '第{n}节', 'ja': '{n}節', 'de': 'Abschnitt {n}'},
    'ref_quote': {'en': '“{t}”', 'zh': '“{t}”', 'ja': '「{t}」', 'de': '„{t}“'},
    'ref_page_after': {'en': ', page {p}', 'zh': '，第{p}页', 'ja': '、{p}ページ', 'de': ', Seite {p}'},
    'ref_page': {'en': 'page {p}', 'zh': '第{p}页', 'ja': '{p}ページ', 'de': 'Seite {p}'},
    # the heading of the name column of a knob table the builder rebuilds (build_manual.knobify)
    'th_name': {'en': 'Name', 'zh': '名称', 'ja': '名前', 'de': 'Name'},
    # a list the builder makes (the screensavers' names)
    'list_sep': {'en': ', ', 'zh': '、', 'ja': '、', 'de': ', '},
    # the date of the build, where the text says {{BUILD}}
    'build_name': {'en': 'AERIA OS, 12 November 2026', 'zh': 'AERIA OS，2026年11月12日', 'ja': 'AERIA OS、2026年11月12日', 'de': 'AERIA OS, 12. November 2026'},
    # the index: the heading over the entries that start with a CJK character, and those that start with kana
    'ix_han': {'en': '#', 'zh': '中文', 'ja': '漢字', 'de': '#'},
    'ix_kana': {'en': '#', 'zh': '假名', 'ja': 'かな', 'de': '#'},
    'ix_digits': {'en': '#', 'zh': '#', 'ja': '#', 'de': '#'},
}

# the words an index entry may start with that are not sorted by
ARTICLES = {'en': r'^(The|A|An) ', 'de': r'^(Der|Die|Das|Den|Dem|Des|Ein|Eine|Einen|Einem|Einer|Eines) ', 'zh': r'^$^', 'ja': r'^$^'}

# a plain chapter number left in the text (of the edition before the present one): "chapter 12", and the same in a
# translation. group(1) is the word (its first letter decides the capital of the reference), group(2) the number.
OLD_CHAPTER = {'en': r'\b([Cc]hapter) (\d+)\b', 'de': r'\b(Kapitel) (\d+)\b', 'zh': r'(第)\s*(\d+)\s*章', 'ja': r'(第)\s*(\d+)\s*章'}

# "knob A" written plainly in the text: the letter is set as a knob's name. (A translation that writes ((A)) needs none.)
KNOB_WORD = {'en': r'\b([Kk]nobs?) ([A-F])\b(?! to)', 'de': r'\b(Reglers?|Regler[n]?) ([A-F])\b(?! bis)',
             'zh': r'(旋钮)\s?([A-F])(?![A-Za-z])', 'ja': r'(ノブ)\s?([A-F])(?![A-Za-z])'}

# the quick guide's own sections named by number in its text ("section 10"): made into a link to that section
QUICK_SECTION = {'en': r'\b(section (\d+))\b', 'de': r'\b(Abschnitt (\d+))\b', 'zh': r'(第\s*(\d+)\s*节)', 'ja': r'((\d+)\s*節)'}

# what is added to the stylesheet for a language
CSS = {
    'en': '',
    'de': 'body { hyphens: auto; -webkit-hyphens: auto; } h1, h2, h3, h4, th, .fh, .toc, .card .ct, .key, .msg, a.xref { hyphens: manual; -webkit-hyphens: manual; }',
    'zh': '', 'ja': '',
}
CJK_CSS = """
/* CJK: break lines between characters by the rules of the language, no spacing-out of CJK lettering, a little more
   leading, no slanted CJK (italic is only for the knobs, which are lettered in Latin) */
body { line-break: strict; word-break: normal; overflow-wrap: break-word; line-height: 1.62; }
body *:not(h1):not(.key):not(.msg):not(.mk):not(i.knob) { letter-spacing: normal !important; }
.manref::before, .cover .big { letter-spacing: 0.06em !important; }
em, i:not(.knob) { font-style: normal; }
svg text, svg tspan { font-family: %FONT%; }
.key { margin: 0 0.14em; }
i.knob { margin: 0 0.08em; }
.msg { margin: 0 0.1em; }
a.xref { white-space: normal; } a.xref .pg { white-space: nowrap; }
td.k, th.k { white-space: normal; }
table.combo th:first-child, table.combo td:first-child { line-height: 1.8; }
"""


def use(code):
    global CODE
    if code not in LANGS:
        raise SystemExit(f'no language {code!r}: {", ".join(LANGS)}')
    CODE = code


def cjk():
    return CODE in ('zh', 'ja')


def S(key, **kw):
    s = STRINGS[key].get(CODE) or STRINGS[key]['en']
    return s.format(**kw) if kw else s


def font():
    return FONTS[CODE]


def css():
    """The language's additions to the stylesheet."""
    out = CSS.get(CODE, '')
    if cjk():
        out += CJK_CSS.replace('%FONT%', font())
    return out


def tr(s):
    """A diagram's lettering in the language in use (the English where there is no translation)."""
    if CODE == 'en' or not s:
        return s
    return DIAGRAMS.get(CODE, {}).get(s, s)


def width(s):
    """The width of a text in Latin letters: a CJK character counts two."""
    return sum(2 if unicodedata.east_asian_width(ch) in 'WF' else 1 for ch in s)


def is_kana(ch):
    return '぀' <= ch <= 'ヿ' or 'ㇰ' <= ch <= 'ㇿ' or 'ｦ' <= ch <= 'ﾟ'


def is_han(ch):
    return '㐀' <= ch <= '鿿' or '豈' <= ch <= '﫿'


def index_group(term):
    """The heading an index entry stands under: its first letter (Ä under A), # for a digit or a sign, and for an
    entry starting with a CJK character the language's heading for those."""
    ch = term[0]
    base = unicodedata.normalize('NFKD', ch)[0].upper()
    if 'A' <= base <= 'Z':
        return base
    if is_kana(ch):
        return S('ix_kana')
    if is_han(ch):
        return S('ix_han')
    return S('ix_digits')


def index_key(term):
    """How the index sorts: Latin entries A to Z (accents and signs ignored), then kana (katakana as hiragana), then
    the CJK characters in their order in Unicode (by radical and stroke count), then the rest."""
    ch = term[0]
    base = ''.join(c for c in unicodedata.normalize('NFKD', term) if not unicodedata.combining(c)).lower().replace('ß', 'ss')
    if 'a' <= base[0] <= 'z':
        return (0, re.sub(r'[^a-z0-9]', '', base), term)
    if base[0].isdigit():
        return (1, re.sub(r'[^a-z0-9]', '', base), term)
    if is_kana(ch):
        return (2, ''.join(chr(ord(c) - 0x60) if 'ァ' <= c <= 'ヶ' else c for c in term), term)
    if is_han(ch):
        return (3, term, term)
    return (4, term, term)


# ---------------------------------------------------------------------------------------------------------------
# The lettering of the diagrams (diagrams.py), keyed by the English. A "|" breaks the line, as in the English.
# Names on the unit and on its screen (keys, knobs, pages, effects, weathers, sockets) stay in English.
DIAGRAMS = {'zh': {}, 'ja': {}, 'de': {}}

DIAGRAMS['zh'] = {
    # flow_sky
    'WHAT GOES IN': '输入',
    'THE CHAIN: UP TO SEVEN EFFECTS, PLACED ON THE TWO DSP CORES': '效果链：最多七个效果，分布在两个 DSP 核心上',
    'THE MASTER SECTION AND THE OUTPUTS (THE WEATHER NEVER MOVES IT)': '主控部分与输出（天气从不改变它）',
    'Inputs A B': '输入 A B', 'Inputs C D': '输入 C D',
    'gain AB, stereo or mono': 'AB 增益，立体声或单声道', 'gain CD, stereo or mono': 'CD 增益，立体声或单声道',
    'one looping sample': '一个循环采样', '16 one-shots': '16 个单次采样', '3 recordings, long grains': '3 段录音，长颗粒',
    'each source has its own SEND': '每个声源都有自己的 SEND',
    'GAIN page': 'GAIN 页', 'slot': '槽位',
    'Each slot holds one of the sixteen effects, or is empty.|The weather moves the effects&#8217; knobs; scenes and the crossfader too.':
        '每个槽位放一个效果（共十六种），或留空。|天气会转动效果的旋钮；场景和交叉推子也会。',
    'a slot taken out as a branch': '把一个槽位作为分支引出',
    'main feedback: FDBK, FTON, WARM (MIX page)': '主反馈：FDBK、FTON、WARM（MIX 页）',
    'Weather|filters': '天气|滤波器', 'rain, thunder,|lightning, HELI': '雨、雷、|闪电、HELI',
    'Freeze|delay': '冻结|延迟', 'ping-pong, WET': '乒乓，WET', 'level, balance': '电平、平衡',
    '4-band|EQ': '四段|均衡', 'Bass|mono': '低音|单声道', 'under 50 Hz': '50 Hz 以下', 'Limiter': '限幅器',
    'slots routed to CUE or panned toward it (with nothing routed there: a copy of MAIN)': '路由到 CUE 或声像偏向 CUE 的槽位（若没有：MAIN 的副本）',
    'USB 1-4: the four inputs': 'USB 1-4：四路输入',
    'The signal through the multi-effect': '多效果器中的信号流',
    # flow_seq
    'MAIN PROCESSOR: EACH TRACK': '主处理器：每条轨道',
    'DSP: A CHANNEL FOR EACH TRACK': 'DSP：每条轨道一个通道',
    'THE MASTER SECTION, SHARED WITH THE MULTI-EFFECT': '主控部分，与多效果器共用',
    'Machine': '音源', 'SMPL  sample|SUB  sine|FM  two sines|NOIS  noise|PLUK  string|MIDI  no sound': 'SMPL  采样|SUB  正弦波|FM  两个正弦波|NOIS  噪声|PLUK  弦|MIDI  无声',
    'drive': '过载', 'crush': '降位', 'VOL, ducking': 'VOL，闪避', 'level, DUCK': '电平，DUCK',
    'each step: velocity, locks,|ratchet, chance, condition': '每一步：力度、锁定、|连击、概率、条件',
    'note or chord, GATE': '音符或和弦，GATE', 'DLY send (after the level)': 'DLY 发送（电平之后）',
    'One effect': '一个效果', 'any of the sixteen,|or none': '十六种之一，|或不用', 'equal power': '等功率',
    'STEM to USB': '分轨到 USB', 'after the effect, before the pan': '效果之后，声像之前',
    'tracks 1-8 on USB channels 3-18 (the 18-channel format)': '轨道 1-8 在 USB 通道 3-18（18 通道格式）',
    'filters always there,|resonance fixed at 20 %': '滤波器始终存在，|共振固定为 20 %',
    'x 8 tracks,|summed': '× 8 条轨道，|相加',
    'Freeze delay': '冻结延迟', 'fed by the DLY sends': '由 DLY 发送馈入', '4-band EQ': '四段均衡', 'Bass mono': '低音单声道',
    'the eight tracks, summed': '八条轨道相加', 'USB: main pair': 'USB：主输出对',
    'Inputs A B C D': '输入 A B C D', 'pass dry to the output': '干声直通到输出',
    'tracks with OUT at CUE or BOTH|(not through the master section)': 'OUT 设为 CUE 或 BOTH 的轨道|（不经过主控部分）',
    'The signal through the sequencer': '音序器中的信号流',
    # memory
    'BATTERY-BACKED MEMORY: KEPT WHILE THE POWER IS OFF': '电池供电存储器：断电后仍保留',
    'THE COMPACTFLASH CARD': 'COMPACTFLASH 卡', 'NOT KEPT': '不保留',
    'The saved state': '保存的状态', 'every knob, the chain,|weathers, master, scale|written 2 s after a change': '每个旋钮、效果链、|天气、主控、音阶|改动后 2 秒写入',
    '16 scenes': '16 个场景', 'and which two the|crossfader holds|written 2 s after a change': '以及交叉推子|两端是哪两个|改动后 2 秒写入',
    'The sequence': '音序', '4 patterns, 8 tracks,|effects, mutes, chain|kept at every touch': '4 个样式、8 条轨道、|效果、静音、链|每次操作即保存',
    'Which samples are loaded': '载入了哪些采样',
    'the loop, each bank&#8217;s folder, the kit, PONG&#8217;s folder: names only': '循环、每个音色库的文件夹、套件、PONG 的文件夹：只存名称',
    '16 projects, each with a name': '16 个工程，各有名称',
    'a project is a copy of all four of the above|PROJ, a trig, FUNC+YES saves  &#183;  YES loads': '工程是以上四项的副本|PROJ、一个 trig、FUNC+YES 保存  &#183;  YES 载入',
    'save copies down,': '保存时向下复制，', 'load copies back up': '载入时向上复制',
    'Your WAV files': '你的 WAV 文件', 'loops, folders of stars, kits, PONG folders': '循环、星星文件夹、套件、PONG 文件夹',
    'AERIA OS only reads the card. A project keeps the|places of its samples, not their sound: the card|must hold the same files in the same folders.':
        'AERIA OS 只读取存储卡。工程记住的是采样的|位置而不是声音本身：卡上必须在同样的|文件夹里放着同样的文件。',
    'read again at start-up and when a project loads': '开机时和载入工程时重新读取',
    'The balloons&#8217; recordings (sample memory)|Whether the delay is frozen|Whether the sequencer was playing|The settings of GUMBALLS and PITCH|What was copied with FUNC + REC':
        '气球的录音（采样内存）|延迟是否被冻结|音序器是否在播放|GUMBALLS 和 PITCH 的设置|用 FUNC + REC 复制的内容',
    'What is kept where': '什么保存在哪里',
    # faces
    'THE SKY': '天空', 'THE SEQUENCER': '音序器',
    'the multi-effect|inputs and sources through a chain of|up to seven effects, moved by the weather||pages: CHAIN SKY SOURCE LOOP STARS BALLOON|MIX GAIN MASTER OUT CLOUD MIDI SYSTEM':
        '多效果器|输入和声源经过最多七个效果的链，|由天气推动||页面：CHAIN SKY SOURCE LOOP STARS BALLOON|MIX GAIN MASTER OUT CLOUD MIDI SYSTEM',
    'the groove box|eight tracks on the sixteen trigs,|each with filters and one effect||pages: SRC AMP MOD FLT FX GEN MIDI PTN|the mixer, MASTER':
        '律动机|十六个 trig 上的八条轨道，|每条带滤波器和一个效果||页面：SRC AMP MOD FLT FX GEN MIDI PTN|调音台、MASTER',
    'Shared by both: the master section (freeze delay, EQ, bass mono, limiter), the tempo, projects, the card, USB, the screensavers':
        '两者共用：主控部分（冻结延迟、均衡、低音单声道、限幅器）、速度、工程、存储卡、USB、屏幕保护',
    'The two faces of AERIA OS': 'AERIA OS 的两副面孔',
    # step_marks, ratchet_curve
    'A trig': '一个 trig', 'Locks': '锁定', 'Chance or|condition': '概率或|条件', 'Ratchet': '连击',
    'Plays|backward': '反向|播放', 'Late|(micro-timing)': '延后|（微定时）', 'Empty step': '空步', 'Empty,|on a beat': '空步，|在拍点上',
    'The marks on a step': '步上的标记',
    'one step (a sixteenth note) from left to right: RTCH 8': '从左到右为一步（十六分音符）：RTCH 8',
    'Ratchets and their curve': '连击及其曲线',
    # smear
    'THE WET PATH: 1,280 SAMPLES (29 MS) LATE': '湿声路径：延迟 1,280 个采样（29 毫秒）',
    'Transform': '变换', '1,024 points,|4 frames overlap,|512 bands a side': '1,024 点，|4 帧重叠，|每侧 512 频带',
    'quiet bands|out': '去除|安静频带', 'each band held,|then fading': '每个频带保持，|然后衰减',
    'live down,|the held in': '实时声降低，|保持声进入', 'Back to sound': '还原为声音',
    'the wash: scattered|phases; WIDE: its|own on each side': '弥散：打散的|相位；WIDE：左右|各自独立',
    'around|1 kHz': '以 1 kHz|为中心', 'dry / wet': '干 / 湿',
    'the dry path: not delayed': '干声路径：无延迟',
    'SMR and FRZ follow the weather: the snow freezes, the heat smears.': 'SMR 和 FRZ 跟随天气：雪会冻结，热会涂抹。',
    'SMEAR: transform, gate, sustain, freeze, back to sound, tilt, mix': 'SMEAR：变换、门限、延音、冻结、还原、倾斜、混合',
    # hookup
    'WHAT COMES OUT': '输出', 'running AERIA OS': '运行 AERIA OS',
    'Synth, drum machine,|microphone preamp': '合成器、鼓机、|话筒前置放大器',
    'A second instrument,|or a stereo return': '第二件乐器，|或立体声返回',
    'Clock, controller,|another sequencer': '时钟、控制器、|另一台音序器',
    'CompactFlash card': 'CompactFlash 卡', 'your WAV files': '你的 WAV 文件',
    'Power supply': '电源', 'the unit&#8217;s own': '本机原配',
    'Mixer, speakers': '调音台、音箱', 'after the master section': '经过主控部分',
    'A second pair': '第二对输出', 'quad, or a cue mix': '四声道，或监听混音',
    'Headphones': '耳机', 'main and cue together': '主输出与监听一起',
    'Synth, drum machine': '合成器、鼓机', 'notes, clock, programs': '音符、时钟、音色切换',
    'The next device': '下一台设备', 'a copy of MIDI In': 'MIDI In 的副本',
    'Computer': '电脑', 'audio, 6 or 18 channels  ·  the card as a disk  ·  update tools': '音频 6 或 18 通道  ·  存储卡当磁盘  ·  更新工具',
    'sound': '声音', 'MIDI, on the 5-pin sockets': 'MIDI，在 5 针插座上',
    'There is no MIDI over USB:|MIDI is on the three|round sockets.': 'USB 不传 MIDI：|MIDI 在三个|圆形插座上。',
    'What to connect where': '什么接到哪里',
    # usb_formats
    '6 CHANNELS  ·  the format a computer picks by itself': '6 通道  ·  电脑自动选择的格式',
    'Input A': '输入 A', 'Input B': '输入 B', 'Input C': '输入 C', 'Input D': '输入 D',
    'your playing as it comes in,|and AERIA&#8217;s result, side by side': '你输入的演奏，|和 AERIA 的结果，并排',
    '18 CHANNELS  ·  choose it on the computer: stems of the sequencer': '18 通道  ·  在电脑上选择：音序器的分轨',
    'Track 1': '轨道 1', 'Track 2': '轨道 2', 'Track 3': '轨道 3', 'Track 4': '轨道 4',
    'Track 5': '轨道 5', 'Track 6': '轨道 6', 'Track 7': '轨道 7', 'Track 8': '轨道 8',
    'a stem is its track after its filters and its effect, before its pan; the master section is in the MAIN pair only':
        '分轨是轨道经过滤波器和效果、声像之前的声音；主控部分只作用于 MAIN 这一对',
    'The two USB audio formats': '两种 USB 音频格式',
    # seq_map
    'the six knobs of|the track&#8217;s effect': '轨道效果的|六个旋钮', 'the effect&#8217;s second set': '效果的第二组',
    'The key opens the page.  The same key again, or FUNC held: its second set (orange).  A step held: VEL MICR PROB COND RTCH CURV.':
        '按键打开页面。再按同一键或按住 FUNC：第二组（橙色）。按住一步：VEL MICR PROB COND RTCH CURV。',
    'The sequencer knob pages': '音序器的旋钮页面',
}

DIAGRAMS['ja'] = {
    # flow_sky
    'WHAT GOES IN': '入力',
    'THE CHAIN: UP TO SEVEN EFFECTS, PLACED ON THE TWO DSP CORES': 'チェーン：最大 7 つのエフェクトを 2 つの DSP コアに配置',
    'THE MASTER SECTION AND THE OUTPUTS (THE WEATHER NEVER MOVES IT)': 'マスターセクションと出力（天気は動かさない）',
    'Inputs A B': '入力 A B', 'Inputs C D': '入力 C D',
    'gain AB, stereo or mono': 'AB ゲイン、ステレオ/モノ', 'gain CD, stereo or mono': 'CD ゲイン、ステレオ/モノ',
    'one looping sample': 'ループするサンプル 1 つ', '16 one-shots': 'ワンショット 16 個', '3 recordings, long grains': '録音 3 つ、長いグレイン',
    'each source has its own SEND': '各ソースに専用の SEND',
    'GAIN page': 'GAIN ページ', 'slot': 'スロット',
    'Each slot holds one of the sixteen effects, or is empty.|The weather moves the effects&#8217; knobs; scenes and the crossfader too.':
        '各スロットには 16 種のエフェクトの 1 つが入るか、空です。|天気がエフェクトのノブを動かします。シーンとクロスフェーダーも同様。',
    'a slot taken out as a branch': 'スロットを分岐として取り出す',
    'main feedback: FDBK, FTON, WARM (MIX page)': 'メインフィードバック：FDBK、FTON、WARM（MIX ページ）',
    'Weather|filters': '天気|フィルター', 'rain, thunder,|lightning, HELI': '雨、雷、|稲妻、HELI',
    'Freeze|delay': 'フリーズ|ディレイ', 'ping-pong, WET': 'ピンポン、WET', 'level, balance': 'レベル、バランス',
    '4-band|EQ': '4 バンド|EQ', 'Bass|mono': 'ベース|モノ', 'under 50 Hz': '50 Hz 以下', 'Limiter': 'リミッター',
    'slots routed to CUE or panned toward it (with nothing routed there: a copy of MAIN)': 'CUE に送られた、または CUE 側へパンされたスロット（何もなければ MAIN のコピー）',
    'USB 1-4: the four inputs': 'USB 1-4：4 つの入力',
    'The signal through the multi-effect': 'マルチエフェクトの信号の流れ',
    # flow_seq
    'MAIN PROCESSOR: EACH TRACK': 'メインプロセッサー：各トラック',
    'DSP: A CHANNEL FOR EACH TRACK': 'DSP：トラックごとのチャンネル',
    'THE MASTER SECTION, SHARED WITH THE MULTI-EFFECT': 'マスターセクション（マルチエフェクトと共用）',
    'Machine': 'マシン', 'SMPL  sample|SUB  sine|FM  two sines|NOIS  noise|PLUK  string|MIDI  no sound': 'SMPL  サンプル|SUB  サイン波|FM  2 つのサイン波|NOIS  ノイズ|PLUK  弦|MIDI  音なし',
    'drive': 'ドライブ', 'crush': 'クラッシュ', 'VOL, ducking': 'VOL、ダッキング', 'level, DUCK': 'レベル、DUCK',
    'each step: velocity, locks,|ratchet, chance, condition': '各ステップ：ベロシティ、ロック、|ラチェット、確率、条件',
    'note or chord, GATE': 'ノートかコード、GATE', 'DLY send (after the level)': 'DLY センド（レベルの後）',
    'One effect': 'エフェクト 1 つ', 'any of the sixteen,|or none': '16 種のどれか、|またはなし', 'equal power': '等パワー',
    'STEM to USB': 'ステムを USB へ', 'after the effect, before the pan': 'エフェクトの後、パンの前',
    'tracks 1-8 on USB channels 3-18 (the 18-channel format)': 'トラック 1-8 は USB チャンネル 3-18（18 チャンネル形式）',
    'filters always there,|resonance fixed at 20 %': 'フィルターは常にあり、|レゾナンスは 20 % 固定',
    'x 8 tracks,|summed': '× 8 トラック、|合算',
    'Freeze delay': 'フリーズディレイ', 'fed by the DLY sends': 'DLY センドから', '4-band EQ': '4 バンド EQ', 'Bass mono': 'ベースモノ',
    'the eight tracks, summed': '8 トラックの合算', 'USB: main pair': 'USB：メインペア',
    'Inputs A B C D': '入力 A B C D', 'pass dry to the output': 'ドライのまま出力へ',
    'tracks with OUT at CUE or BOTH|(not through the master section)': 'OUT が CUE か BOTH のトラック|（マスターセクションを通らない）',
    'The signal through the sequencer': 'シーケンサーの信号の流れ',
    # memory
    'BATTERY-BACKED MEMORY: KEPT WHILE THE POWER IS OFF': 'バッテリーバックアップメモリー：電源オフでも保持',
    'THE COMPACTFLASH CARD': 'COMPACTFLASH カード', 'NOT KEPT': '保持されない',
    'The saved state': '保存された状態', 'every knob, the chain,|weathers, master, scale|written 2 s after a change': 'すべてのノブ、チェーン、|天気、マスター、スケール|変更の 2 秒後に書き込み',
    '16 scenes': '16 のシーン', 'and which two the|crossfader holds|written 2 s after a change': 'クロスフェーダーが|持つ 2 つも|変更の 2 秒後に書き込み',
    'The sequence': 'シーケンス', '4 patterns, 8 tracks,|effects, mutes, chain|kept at every touch': '4 パターン、8 トラック、|エフェクト、ミュート、チェーン|操作のたびに保存',
    'Which samples are loaded': '読み込まれているサンプル',
    'the loop, each bank&#8217;s folder, the kit, PONG&#8217;s folder: names only': 'ループ、各バンクのフォルダー、キット、PONG のフォルダー：名前のみ',
    '16 projects, each with a name': '名前付きの 16 プロジェクト',
    'a project is a copy of all four of the above|PROJ, a trig, FUNC+YES saves  &#183;  YES loads': 'プロジェクトは上の 4 つすべてのコピー|PROJ、トリグ、FUNC+YES で保存  &#183;  YES で読み込み',
    'save copies down,': '保存で下へコピー、', 'load copies back up': '読み込みで上へコピー',
    'Your WAV files': 'あなたの WAV ファイル', 'loops, folders of stars, kits, PONG folders': 'ループ、スターのフォルダー、キット、PONG フォルダー',
    'AERIA OS only reads the card. A project keeps the|places of its samples, not their sound: the card|must hold the same files in the same folders.':
        'AERIA OS はカードを読むだけです。プロジェクトが|覚えるのはサンプルの場所で、音ではありません。|カードの同じフォルダーに同じファイルが必要です。',
    'read again at start-up and when a project loads': '起動時とプロジェクト読み込み時に再読み込み',
    'The balloons&#8217; recordings (sample memory)|Whether the delay is frozen|Whether the sequencer was playing|The settings of GUMBALLS and PITCH|What was copied with FUNC + REC':
        'バルーンの録音（サンプルメモリー）|ディレイがフリーズ中かどうか|シーケンサーが再生中だったか|GUMBALLS と PITCH の設定|FUNC + REC でコピーした内容',
    'What is kept where': '何がどこに保存されるか',
    # faces
    'THE SKY': 'スカイ', 'THE SEQUENCER': 'シーケンサー',
    'the multi-effect|inputs and sources through a chain of|up to seven effects, moved by the weather||pages: CHAIN SKY SOURCE LOOP STARS BALLOON|MIX GAIN MASTER OUT CLOUD MIDI SYSTEM':
        'マルチエフェクト|入力とソースが最大 7 つのエフェクトの|チェーンを通り、天気が動かす||ページ：CHAIN SKY SOURCE LOOP STARS BALLOON|MIX GAIN MASTER OUT CLOUD MIDI SYSTEM',
    'the groove box|eight tracks on the sixteen trigs,|each with filters and one effect||pages: SRC AMP MOD FLT FX GEN MIDI PTN|the mixer, MASTER':
        'グルーヴボックス|16 のトリグに 8 トラック、|それぞれフィルターとエフェクト 1 つ||ページ：SRC AMP MOD FLT FX GEN MIDI PTN|ミキサー、MASTER',
    'Shared by both: the master section (freeze delay, EQ, bass mono, limiter), the tempo, projects, the card, USB, the screensavers':
        '両方で共有：マスターセクション（フリーズディレイ、EQ、ベースモノ、リミッター）、テンポ、プロジェクト、カード、USB、スクリーンセーバー',
    'The two faces of AERIA OS': 'AERIA OS の 2 つの顔',
    # step_marks, ratchet_curve
    'A trig': 'トリグ', 'Locks': 'ロック', 'Chance or|condition': '確率または|条件', 'Ratchet': 'ラチェット',
    'Plays|backward': '逆再生', 'Late|(micro-timing)': '遅れ|（マイクロタイミング）', 'Empty step': '空のステップ', 'Empty,|on a beat': '空、|拍の位置',
    'The marks on a step': 'ステップのマーク',
    'one step (a sixteenth note) from left to right: RTCH 8': '左から右まで 1 ステップ（16 分音符）：RTCH 8',
    'Ratchets and their curve': 'ラチェットとそのカーブ',
    # smear
    'THE WET PATH: 1,280 SAMPLES (29 MS) LATE': 'ウェットの経路：1,280 サンプル（29 ms）遅れ',
    'Transform': '変換', '1,024 points,|4 frames overlap,|512 bands a side': '1,024 ポイント、|4 フレーム重ね、|片側 512 バンド',
    'quiet bands|out': '静かなバンド|を除去', 'each band held,|then fading': 'バンドを保持、|やがて減衰',
    'live down,|the held in': 'ライブを下げ、|保持音を入れる', 'Back to sound': '音に戻す',
    'the wash: scattered|phases; WIDE: its|own on each side': '散らした位相|WIDE：左右で|別々',
    'around|1 kHz': '1 kHz|中心', 'dry / wet': 'Dry / Wet',
    'the dry path: not delayed': 'ドライの経路：遅れなし',
    'SMR and FRZ follow the weather: the snow freezes, the heat smears.': 'SMR と FRZ は天気に従う：雪は凍らせ、暑さはにじませる。',
    'SMEAR: transform, gate, sustain, freeze, back to sound, tilt, mix': 'SMEAR：変換、ゲート、サステイン、フリーズ、音に戻す、ティルト、ミックス',
    # hookup
    'WHAT COMES OUT': '出力', 'running AERIA OS': 'AERIA OS で動作',
    'Synth, drum machine,|microphone preamp': 'シンセ、ドラムマシン、|マイクプリアンプ',
    'A second instrument,|or a stereo return': '2 台目の楽器、|またはステレオリターン',
    'Clock, controller,|another sequencer': 'クロック、コントローラー、|別のシーケンサー',
    'CompactFlash card': 'CompactFlash カード', 'your WAV files': 'あなたの WAV ファイル',
    'Power supply': '電源', 'the unit&#8217;s own': '本体付属のもの',
    'Mixer, speakers': 'ミキサー、スピーカー', 'after the master section': 'マスターセクションの後',
    'A second pair': '2 組目', 'quad, or a cue mix': 'クアッド、またはキューミックス',
    'Headphones': 'ヘッドホン', 'main and cue together': 'メインとキューを一緒に',
    'Synth, drum machine': 'シンセ、ドラムマシン', 'notes, clock, programs': 'ノート、クロック、プログラム',
    'The next device': '次の機器', 'a copy of MIDI In': 'MIDI In のコピー',
    'Computer': 'コンピューター', 'audio, 6 or 18 channels  ·  the card as a disk  ·  update tools': 'オーディオ 6/18 ch  ·  カードをディスクに  ·  更新',
    'sound': '音声', 'MIDI, on the 5-pin sockets': 'MIDI（5 ピン端子）',
    'There is no MIDI over USB:|MIDI is on the three|round sockets.': 'USB では MIDI は|使えません。MIDI は|3 つの丸い端子です。',
    'What to connect where': '何をどこにつなぐか',
    # usb_formats
    '6 CHANNELS  ·  the format a computer picks by itself': '6 チャンネル  ·  コンピューターが自動で選ぶ形式',
    'Input A': '入力 A', 'Input B': '入力 B', 'Input C': '入力 C', 'Input D': '入力 D',
    'your playing as it comes in,|and AERIA&#8217;s result, side by side': '入ってくる演奏と、|AERIA の結果を並べて',
    '18 CHANNELS  ·  choose it on the computer: stems of the sequencer': '18 チャンネル  ·  コンピューター側で選択：シーケンサーのステム',
    'Track 1': 'トラック 1', 'Track 2': 'トラック 2', 'Track 3': 'トラック 3', 'Track 4': 'トラック 4',
    'Track 5': 'トラック 5', 'Track 6': 'トラック 6', 'Track 7': 'トラック 7', 'Track 8': 'トラック 8',
    'a stem is its track after its filters and its effect, before its pan; the master section is in the MAIN pair only':
        'ステムはフィルターとエフェクトの後、パンの前のトラックの音。マスターセクションは MAIN ペアのみ',
    'The two USB audio formats': '2 つの USB オーディオ形式',
    # seq_map
    'the six knobs of|the track&#8217;s effect': 'トラックの|エフェクトの 6 ノブ', 'the effect&#8217;s second set': 'エフェクトの第 2 セット',
    'The key opens the page.  The same key again, or FUNC held: its second set (orange).  A step held: VEL MICR PROB COND RTCH CURV.':
        'キーでページを開く。同じキーか FUNC：第2セット（橙）。ステップ長押し：VEL MICR PROB COND RTCH CURV',
    'The sequencer knob pages': 'シーケンサーのノブページ',
}

DIAGRAMS['de'] = {
    # flow_sky
    'WHAT GOES IN': 'WAS HINEINGEHT',
    'THE CHAIN: UP TO SEVEN EFFECTS, PLACED ON THE TWO DSP CORES': 'DIE KETTE: BIS ZU SIEBEN EFFEKTE AUF DEN ZWEI DSP-KERNEN',
    'THE MASTER SECTION AND THE OUTPUTS (THE WEATHER NEVER MOVES IT)': 'MASTER-SEKTION UND AUSGÄNGE (DAS WETTER BEWEGT SIE NIE)',
    'Inputs A B': 'Eingänge A B', 'Inputs C D': 'Eingänge C D',
    'gain AB, stereo or mono': 'Gain AB, stereo/mono', 'gain CD, stereo or mono': 'Gain CD, stereo/mono',
    'one looping sample': 'ein Loop-Sample', '16 one-shots': '16 One-Shots', '3 recordings, long grains': '3 Aufnahmen, Grains',
    'each source has its own SEND': 'jede Quelle hat ihren SEND',
    'GAIN page': 'Seite GAIN', 'slot': 'Slot',
    'Each slot holds one of the sixteen effects, or is empty.|The weather moves the effects&#8217; knobs; scenes and the crossfader too.':
        'Jeder Slot hält einen der sechzehn Effekte oder ist leer.|Das Wetter dreht die Regler der Effekte, Szenen und Crossfader auch.',
    'a slot taken out as a branch': 'ein Slot als Abzweig',
    'main feedback: FDBK, FTON, WARM (MIX page)': 'Haupt-Feedback: FDBK, FTON, WARM (Seite MIX)',
    'Weather|filters': 'Wetter-|filter', 'rain, thunder,|lightning, HELI': 'Regen, Donner,|Blitz, HELI',
    'Freeze|delay': 'Freeze-|Delay', 'ping-pong, WET': 'Ping-Pong, WET', 'level, balance': 'Pegel, Balance',
    '4-band|EQ': '4-Band-|EQ', 'Bass|mono': 'Bass|mono', 'under 50 Hz': 'unter 50 Hz', 'Limiter': 'Limiter',
    'slots routed to CUE or panned toward it (with nothing routed there: a copy of MAIN)': 'Slots, die nach CUE gehen oder dorthin gepannt sind (sonst eine Kopie von MAIN)',
    'USB 1-4: the four inputs': 'USB 1-4: die vier Eingänge',
    'The signal through the multi-effect': 'Das Signal durch den Multieffekt',
    # flow_seq
    'MAIN PROCESSOR: EACH TRACK': 'HAUPTPROZESSOR: JEDE SPUR',
    'DSP: A CHANNEL FOR EACH TRACK': 'DSP: EIN KANAL JE SPUR',
    'THE MASTER SECTION, SHARED WITH THE MULTI-EFFECT': 'DIE MASTER-SEKTION, GETEILT MIT DEM MULTIEFFEKT',
    'Machine': 'Maschine', 'SMPL  sample|SUB  sine|FM  two sines|NOIS  noise|PLUK  string|MIDI  no sound': 'SMPL  Sample|SUB  Sinus|FM  zwei Sinus|NOIS  Rauschen|PLUK  Saite|MIDI  kein Klang',
    'drive': 'Drive', 'crush': 'Crush', 'VOL, ducking': 'VOL, Ducking', 'level, DUCK': 'Pegel, DUCK',
    'each step: velocity, locks,|ratchet, chance, condition': 'je Step: Velocity, Locks,|Ratchet, Chance, Bedingung',
    'note or chord, GATE': 'Note/Akkord, GATE', 'DLY send (after the level)': 'DLY-Send (nach dem Pegel)',
    'One effect': 'Ein Effekt', 'any of the sixteen,|or none': 'einer der sechzehn|oder keiner', 'equal power': 'Equal Power',
    'STEM to USB': 'STEM an USB', 'after the effect, before the pan': 'nach dem Effekt, vor dem Pan',
    'tracks 1-8 on USB channels 3-18 (the 18-channel format)': 'Spuren 1-8 auf USB-Kanälen 3-18 (18-Kanal-Format)',
    'filters always there,|resonance fixed at 20 %': 'Filter immer da,|Resonanz fest 20 %',
    'x 8 tracks,|summed': 'x 8 Spuren,|summiert',
    'Freeze delay': 'Freeze-Delay', 'fed by the DLY sends': 'aus den DLY-Sends', '4-band EQ': '4-Band-EQ', 'Bass mono': 'Bass mono',
    'the eight tracks, summed': 'die acht Spuren, summiert', 'USB: main pair': 'USB: Hauptpaar',
    'Inputs A B C D': 'Eingänge A B C D', 'pass dry to the output': 'trocken zum Ausgang',
    'tracks with OUT at CUE or BOTH|(not through the master section)': 'Spuren mit OUT auf CUE oder BOTH|(nicht durch die Master-Sektion)',
    'The signal through the sequencer': 'Das Signal durch den Sequencer',
    # memory
    'BATTERY-BACKED MEMORY: KEPT WHILE THE POWER IS OFF': 'BATTERIEGEPUFFERTER SPEICHER: BLEIBT OHNE STROM ERHALTEN',
    'THE COMPACTFLASH CARD': 'DIE COMPACTFLASH-KARTE', 'NOT KEPT': 'NICHT GESPEICHERT',
    'The saved state': 'Gespeicherter Zustand', 'every knob, the chain,|weathers, master, scale|written 2 s after a change': 'jeder Regler, die Kette,|Wetter, Master, Skala|nach 2 s gespeichert',
    '16 scenes': '16 Szenen', 'and which two the|crossfader holds|written 2 s after a change': 'und welche zwei der|Crossfader hält|nach 2 s gespeichert',
    'The sequence': 'Die Sequenz', '4 patterns, 8 tracks,|effects, mutes, chain|kept at every touch': '4 Patterns, 8 Spuren,|Effekte, Mutes, Kette|bei jedem Griff gesichert',
    'Which samples are loaded': 'Welche Samples geladen sind',
    'the loop, each bank&#8217;s folder, the kit, PONG&#8217;s folder: names only': 'der Loop, der Ordner jeder Bank, das Kit, PONGs Ordner: nur die Namen',
    '16 projects, each with a name': '16 Projekte, jedes mit Namen',
    'a project is a copy of all four of the above|PROJ, a trig, FUNC+YES saves  &#183;  YES loads': 'ein Projekt ist eine Kopie aller vier oben|PROJ, ein Trig, FUNC+YES speichert  &#183;  YES lädt',
    'save copies down,': 'Speichern kopiert hinab,', 'load copies back up': 'Laden kopiert hinauf',
    'Your WAV files': 'Ihre WAV-Dateien', 'loops, folders of stars, kits, PONG folders': 'Loops, Stern-Ordner, Kits, PONG-Ordner',
    'AERIA OS only reads the card. A project keeps the|places of its samples, not their sound: the card|must hold the same files in the same folders.':
        'AERIA OS liest die Karte nur. Ein Projekt merkt sich|die Orte seiner Samples, nicht ihren Klang: Auf der|Karte müssen dieselben Dateien in denselben Ordnern sein.',
    'read again at start-up and when a project loads': 'neu gelesen beim Start und wenn ein Projekt lädt',
    'The balloons&#8217; recordings (sample memory)|Whether the delay is frozen|Whether the sequencer was playing|The settings of GUMBALLS and PITCH|What was copied with FUNC + REC':
        'Die Aufnahmen der Ballons (Sample-Speicher)|Ob das Delay eingefroren ist|Ob der Sequencer lief|Die Einstellungen von GUMBALLS und PITCH|Was mit FUNC + REC kopiert wurde',
    'What is kept where': 'Was wo gespeichert wird',
    # faces
    'THE SKY': 'DER HIMMEL', 'THE SEQUENCER': 'DER SEQUENCER',
    'the multi-effect|inputs and sources through a chain of|up to seven effects, moved by the weather||pages: CHAIN SKY SOURCE LOOP STARS BALLOON|MIX GAIN MASTER OUT CLOUD MIDI SYSTEM':
        'der Multieffekt|Eingänge und Quellen durch eine Kette aus|bis zu sieben Effekten, bewegt vom Wetter||Seiten: CHAIN SKY SOURCE LOOP STARS BALLOON|MIX GAIN MASTER OUT CLOUD MIDI SYSTEM',
    'the groove box|eight tracks on the sixteen trigs,|each with filters and one effect||pages: SRC AMP MOD FLT FX GEN MIDI PTN|the mixer, MASTER':
        'die Groovebox|acht Spuren auf den sechzehn Trigs,|jede mit Filtern und einem Effekt||Seiten: SRC AMP MOD FLT FX GEN MIDI PTN|der Mixer, MASTER',
    'Shared by both: the master section (freeze delay, EQ, bass mono, limiter), the tempo, projects, the card, USB, the screensavers':
        'Beiden gemeinsam: Master-Sektion (Freeze-Delay, EQ, Bass mono, Limiter), Tempo, Projekte, Karte, USB, Bildschirmschoner',
    'The two faces of AERIA OS': 'Die zwei Gesichter von AERIA OS',
    # step_marks, ratchet_curve
    'A trig': 'Ein Trig', 'Locks': 'Locks', 'Chance or|condition': 'Chance oder|Bedingung', 'Ratchet': 'Ratchet',
    'Plays|backward': 'Spielt|rückwärts', 'Late|(micro-timing)': 'Später|(Micro-Timing)', 'Empty step': 'Leerer Step', 'Empty,|on a beat': 'Leer,|auf dem Schlag',
    'The marks on a step': 'Die Zeichen auf einem Step',
    'one step (a sixteenth note) from left to right: RTCH 8': 'ein Step (eine Sechzehntel) von links nach rechts: RTCH 8',
    'Ratchets and their curve': 'Ratchets und ihre Kurve',
    # smear
    'THE WET PATH: 1,280 SAMPLES (29 MS) LATE': 'DER NASSE WEG: 1.280 SAMPLES (29 MS) SPÄTER',
    'Transform': 'Transformation', '1,024 points,|4 frames overlap,|512 bands a side': '1.024 Punkte,|4 Frames überlappt,|512 Bänder je Seite',
    'quiet bands|out': 'leise Bänder|raus', 'each band held,|then fading': 'Band gehalten,|dann verklingend',
    'live down,|the held in': 'Live leiser,|Gehaltenes rein', 'Back to sound': 'Zurück zu Klang',
    'the wash: scattered|phases; WIDE: its|own on each side': 'Schleier: Phasen|verstreut; WIDE:|je Seite eigene',
    'around|1 kHz': 'um|1 kHz', 'dry / wet': 'Dry / Wet',
    'the dry path: not delayed': 'der trockene Weg: nicht verzögert',
    'SMR and FRZ follow the weather: the snow freezes, the heat smears.': 'SMR und FRZ folgen dem Wetter: Schnee friert ein, Hitze verschmiert.',
    'SMEAR: transform, gate, sustain, freeze, back to sound, tilt, mix': 'SMEAR: Transformation, Gate, Sustain, Freeze, zurück zu Klang, Tilt, Mix',
    # hookup
    'WHAT COMES OUT': 'WAS HERAUSKOMMT', 'running AERIA OS': 'mit AERIA OS',
    'Synth, drum machine,|microphone preamp': 'Synth, Drumcomputer,|Mikrofon-Vorverstärker',
    'A second instrument,|or a stereo return': 'Ein zweites Instrument|oder ein Stereo-Return',
    'Clock, controller,|another sequencer': 'Clock, Controller,|ein anderer Sequencer',
    'CompactFlash card': 'CompactFlash-Karte', 'your WAV files': 'Ihre WAV-Dateien',
    'Power supply': 'Netzteil', 'the unit&#8217;s own': 'das des Geräts',
    'Mixer, speakers': 'Mischpult, Boxen', 'after the master section': 'nach der Master-Sektion',
    'A second pair': 'Ein zweites Paar', 'quad, or a cue mix': 'Quad oder Cue-Mix',
    'Headphones': 'Kopfhörer', 'main and cue together': 'Main und Cue zusammen',
    'Synth, drum machine': 'Synth, Drumcomputer', 'notes, clock, programs': 'Noten, Clock, Programme',
    'The next device': 'Das nächste Gerät', 'a copy of MIDI In': 'eine Kopie von MIDI In',
    'Computer': 'Computer', 'audio, 6 or 18 channels  ·  the card as a disk  ·  update tools': 'Audio, 6 oder 18 Kanäle  ·  Karte als Laufwerk  ·  Update',
    'sound': 'Klang', 'MIDI, on the 5-pin sockets': 'MIDI, an den 5-Pol-Buchsen',
    'There is no MIDI over USB:|MIDI is on the three|round sockets.': 'Kein MIDI über USB:|MIDI liegt an den drei|runden Buchsen.',
    'What to connect where': 'Was wo angeschlossen wird',
    # usb_formats
    '6 CHANNELS  ·  the format a computer picks by itself': '6 KANÄLE  ·  das Format, das ein Computer selbst wählt',
    'Input A': 'Eingang A', 'Input B': 'Eingang B', 'Input C': 'Eingang C', 'Input D': 'Eingang D',
    'your playing as it comes in,|and AERIA&#8217;s result, side by side': 'Ihr Spiel, wie es hereinkommt,|und AERIAs Ergebnis, nebeneinander',
    '18 CHANNELS  ·  choose it on the computer: stems of the sequencer': '18 KANÄLE  ·  am Computer wählen: Stems des Sequencers',
    'Track 1': 'Spur 1', 'Track 2': 'Spur 2', 'Track 3': 'Spur 3', 'Track 4': 'Spur 4',
    'Track 5': 'Spur 5', 'Track 6': 'Spur 6', 'Track 7': 'Spur 7', 'Track 8': 'Spur 8',
    'a stem is its track after its filters and its effect, before its pan; the master section is in the MAIN pair only':
        'ein Stem ist die Spur nach Filtern und Effekt, vor dem Pan; die Master-Sektion liegt nur im MAIN-Paar',
    'The two USB audio formats': 'Die zwei USB-Audioformate',
    # seq_map
    'the six knobs of|the track&#8217;s effect': 'die sechs Regler|des Spur-Effekts', 'the effect&#8217;s second set': 'zweiter Satz des Effekts',
    'The key opens the page.  The same key again, or FUNC held: its second set (orange).  A step held: VEL MICR PROB COND RTCH CURV.':
        'Die Taste öffnet die Seite. Nochmals oder FUNC gehalten: zweiter Satz (orange). Ein Step gehalten: VEL MICR PROB COND RTCH CURV.',
    'The sequencer knob pages': 'Die Reglerseiten des Sequencers',
}
