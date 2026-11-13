"""AERIA HEAD / TAP render rig (runs inside the modwerk-source-tools image).

Modelled on sdk/octabam/modules/ring-sc/verify.py: the effect is assembled with
dsp_asm, disassembled back (refusing mpysu/macsu and label/lua traps), and run
by dsp_host from a SYNTHETIC memory image (no firmware read), beside fixtures
that publish audio where the base's key bus puts it:
  ksloc (ring-sc/tools/keystub.asm, read from the repo): a track's audio on
        this core's bus, Y:$800 + track*$40;
  ksfor (keystub.asm): a track's audio in every generation of the T1-4 core's
        window, Y:$3be00 + local*$80 + gen*$20 (rig5: T1 and T2 for HEAD on T5);
  llloc (LPLOC, here; rig5's chain stub): after HEAD on its buffer, HEAD's output
        x G as T8's FX1 input on this core's bus (Y:$9c0), one block late;
  lpfob (LPSTUB, here; the first build's, unused now): an instance chained AFTER HEAD on the same audio
        buffer, which leaves HEAD's output untouched (so it is what is
        captured) and publishes it, times a chain gain G = knob/32, as T8's
        FX1 input in every generation of the T5-8 core's window
        ($33e00 + 3*$80 + gen*$20): a closed feedback loop with one block
        (16 samples) of latency, the chain T3 FX2 .. T7 modelled as a gain.
What this cannot see: the base's tap and generation counter running, the real
chain's 3 NEIGHBOR hops (~200 samples), the two cores running at once.
"""
import math, os, re, struct, subprocess, pathlib, random

TOOLS = pathlib.Path('/opt/toolchain/vendor/dsp56300/build/source')
DSP_ASM = TOOLS / 'dsp_host/dsp_asm'
DSP_HOST = pathlib.Path(__file__).resolve().parent / '.build/dsp_host'  # built from the image's vendored tree (same dsp_host.cpp as the repo)
DISASM = TOOLS / 'disassemble/dsp56kDisassemble'
KEYSTUB = pathlib.Path('/work/sdk/octabam/modules/ring-sc/tools/keystub.asm')
HERE = pathlib.Path(__file__).resolve().parent
WORK = pathlib.Path(os.environ.get('RIG_WORK', '/tmp/rig'))
WORK.mkdir(parents=True, exist_ok=True)
Q = 1 << 23
FULL = Q - 1
ORG, STUB, LOOP = 0x2000, 0x2800, 0x2c00
LIVE = {0x33dfe: [0x10000, 0], 0x3bdfe: [0x10000, 0]}

LPSTUB = r"""
lpini:
        rts
lpfob:
        move    r0,b
        and     #>$1f,b
        move    b1,n1
        move    #>$33f80,r1
        lua     (r1)+n1,r1
        move    #>$ffffff,m0
        move    #>$ffffff,m1
        move    #>$20,n1
        move    x:(r6+$0),y0
        do      n7,>lp01
        move    x:(r0)+,x0
        mpy     y0,x0,a
        asl     #2,a,a
        move    a,x0
        move    x0,y:(r1)+n1
        move    x0,y:(r1)+n1
        move    x0,y:(r1)+n1
        move    x0,y:(r1)-n1
        move    (r1)-n1
        move    (r1)-n1
        move    (r1)+
        move    x:(r0)+,x0
        mpy     y0,x0,a
        asl     #2,a,a
        move    a,x0
        move    x0,y:(r1)+n1
        move    x0,y:(r1)+n1
        move    x0,y:(r1)+n1
        move    x0,y:(r1)-n1
        move    (r1)-n1
        move    (r1)-n1
        move    (r1)+
lp01:
        rts
"""

# The chain stub for rig5: HEAD's output x (knob0/32) published as T8's FX1 input on THIS core's
# bus, Y:$9c0 + (r0 & $1f). It runs after HEAD in the block, so HEAD reads it one block later, as the base's
# T8 (dispatched after T5) gives it.
LPLOC = r"""
llini:
        rts
llloc:
        move    r0,b
        and     #>$1f,b
        move    b1,n1
        move    #>$9c0,r1
        lua     (r1)+n1,r1
        move    #>$ffffff,m0
        move    #>$ffffff,m1
        move    x:(r6+$0),y0
        do      n7,>ll01
        move    x:(r0)+,x0
        mpy     y0,x0,a
        asl     #2,a,a
        move    a,x0
        move    x0,y:(r1)+
        move    x:(r0)+,x0
        mpy     y0,x0,a
        asl     #2,a,a
        move    a,x0
        move    x0,y:(r1)+
ll01:
        rts
"""


def assemble(text, org, tag):
    src, blob, sym = WORK / f'{tag}.asm', WORK / f'{tag}.bin', WORK / f'{tag}.sym'
    src.write_text(text)
    r = subprocess.run([str(DSP_ASM), '-in', str(src), '-org', f'{org:x}', '-out', str(blob), '-sym', str(sym)],
                       capture_output=True, text=True)
    if r.returncode:
        raise SystemExit(f'dsp_asm {tag}: {r.stdout}{r.stderr}')
    syms = {k: int(v, 16) for k, v in (l.split() for l in sym.read_text().splitlines() if l.strip())}
    code = blob.read_bytes()
    return blob, [int.from_bytes(code[i:i + 3], 'little') for i in range(0, len(code), 3)], syms


def disassemble(blob, org):
    r = subprocess.run([str(DISASM), '-in', str(blob), '-pc', f'{org:x}', '-le'], capture_output=True, text=True)
    return [l for l in r.stdout.splitlines() if re.match(r'^[0-9a-f]{6}:', l)]


def source_checks(text):
    """(problems list). Labels: none a prefix of another; no rep; lua and displaced
    moves within -64..63."""
    probs = []
    labels = [l.split(':')[0] for l in text.splitlines() if re.match(r'^[A-Za-z_][A-Za-z0-9_]*:', l)]
    for a in labels:
        for b in labels:
            if a != b and b.startswith(a):
                probs.append(f'label {a} is a prefix of {b}')
    if len(set(labels)) != len(labels):
        probs.append('duplicate labels')
    for n, l in enumerate(text.splitlines(), 1):
        code = l.split(';')[0]
        if re.match(r'\s+rep\b', code):
            probs.append(f'line {n}: rep')
        for m in re.finditer(r'\(r\d([+-])\$?([0-9a-fA-F]+)\)', code):
            v = int(m.group(2), 16) * (1 if m.group(1) == '+' else -1)
            if not -64 <= v <= 63:
                probs.append(f'line {n}: displacement {v} out of -64..63')
    return probs


def audit(lines):
    """Mnemonics that would be wrong signed math (mpysu, macsu, ...uu)."""
    bad = []
    for l in lines:
        m = re.match(r'^[0-9a-f]{6}:\s+(\S+)', l)
        if m and re.match(r'(mpy|mac)(su|uu|us)', m.group(1)):
            bad.append(l.strip())
    return bad


def rec(sp, addr, words):
    return struct.pack('<BII', sp, addr, len(words)) + struct.pack(f'<{len(words)}I', *words)


def write_mem(path, *code, own=3, bus=LIVE):
    """ring-sc verify.py's synthetic image: `own` = the track (1-8) the effect
    sits on (X:$420 = own-1); `bus` = the seed marks and counters in Y."""
    body = b''.join(rec(0, org, words) for org, words in code) + rec(0, 0x40, [0, 0, 0, 0])
    for a, words in bus.items():
        body += rec(2, a, words)
    x = {0x415: 0x700, 0x416: 0x700, 0x419: 0, 0x208: 0x500, 0x20a: 0x6000,
         0x20c: 0, 0x20d: 16, 0x20e: 0, 0x213: 0x256, 0x420: own - 1}
    x.update({0x256 + i: 0x4000 for i in range(16)})
    for a, v in x.items():
        body += rec(1, a, [v])
    path.write_bytes(body + struct.pack('<BII', 0xff, 0, 0))


def host(mem, entries, inputs, params, blocks, extra=(), audioidx=None, tag='run'):
    """entries: [(init, proc)] per instance; inputs: stereo int lists (or None = silence)
    per instance; params: 12-int lists per instance (or a paramfile path str).
    Returns (outputs per instance, stdout)."""
    n = len(entries)
    ins, pf = [], []
    for k, x in enumerate(inputs):
        if x is None:
            ins.append('-')
        else:
            f = WORK / f'{tag}.in{k}'
            f.write_bytes(struct.pack(f'<{len(x)}i', *x))
            ins.append(str(f))
    out = WORK / f'{tag}.out'
    cmd = [str(DSP_HOST), '-mem', str(mem), '-inst', str(n),
           '-init', ','.join(f'{e[0]:x}' for e in entries), '-proc', ','.join(f'{e[1]:x}' for e in entries),
           '-ctx', '40,41,42', '-frames', '16', '-blocks', str(blocks), '-in', ','.join(ins), '-stereo',
           '-out', str(out)]
    # -paramfile streams are matched to instances by ORDER, so once any instance
    # is automated every instance gets a stream (a constant one for the others).
    auto = any(isinstance(p, str) for p in params)
    for k, p in enumerate(params):
        if isinstance(p, str):
            first = pathlib.Path(p).read_text().splitlines()[0].split(',')[1:]
            cmd += ['-params', ','.join(first)]
        else:
            cmd += ['-params', ','.join(map(str, p))]
    if auto:
        for k, p in enumerate(params):
            if not isinstance(p, str):
                f = WORK / f'{tag}.const{k}.csv'
                f.write_text('0,' + ','.join(map(str, list(p) + [0] * (12 - len(p)))) + '\n')
                p = str(f)
            cmd += ['-paramfile', p]
    if audioidx:
        cmd += ['-audioidx', ','.join(map(str, audioidx))]
    cmd += list(extra)
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=900)
    if r.returncode:
        raise SystemExit(f'dsp_host exit {r.returncode}:\n{" ".join(cmd)}\n{r.stdout[-3000:]}{r.stderr}')
    outs = []
    for k in range(n):
        p = pathlib.Path(str(out) + (f'.i{k}' if k else ''))
        data = p.read_bytes() if p.exists() else b''
        outs.append(list(struct.unpack(f'<{len(data) // 4}i', data)))
    return outs, r.stdout


def meter(stdout, core=0):
    """The core meter (every instruction the core executed in a block, harness
    stub included): (worst block, mean block) per sample at 16 frames. The
    per-instance figure dsp_host prints undercounts DO loops, so it is not used."""
    m = re.search(rf'core {core} meter: max (\d+) instructions in block \d+, mean ([0-9.]+)', stdout)
    return int(m.group(1)) / 16, float(m.group(2)) / 16


def per_sample(stdout, k):
    m = re.search(rf'instance {k}: ([0-9.]+) instructions/sample', stdout)
    return float(m.group(1)) if m else None


def strays(stdout):
    return [l for l in stdout.splitlines() if 'stray' in l.lower() or 'clobber' in l.lower()]


# ---- signals ------------------------------------------------------------------
def sine(f, amp, n, phase=0.0):
    return [round(amp * FULL * math.sin(2 * math.pi * f * i / 44100 + phase)) for i in range(n)]


def noise(n, amp, seed=1):
    r = random.Random(seed)
    return [round(amp * FULL * (2 * r.random() - 1)) for _ in range(n)]


def stereo(left, right=None):
    right = left if right is None else right
    return [s for pair in zip(left, right) for s in pair]


def peak(x):
    return max((abs(v) for v in x), default=0) / Q


def rms(x):
    return math.sqrt(sum(v * v for v in x) / max(1, len(x))) / Q


def db(v):
    return 20 * math.log10(max(v, 1e-12))


class Report:
    def __init__(self, name):
        self.name, self.rows, self.fail = name, [], 0

    def check(self, label, ok, detail=''):
        self.rows.append((label, bool(ok), detail))
        print(('PASS ' if ok else 'FAIL ') + label + (' -- ' + str(detail) if detail != '' else ''), flush=True)
        self.fail += not ok

    def note(self, label, detail):
        self.rows.append((label, None, detail))
        print('INFO ' + label + ' -- ' + str(detail), flush=True)

    def save(self, path):
        lines = ['| result | check | detail |', '|---|---|---|']
        for label, ok, d in self.rows:
            lines.append(f"| {'PASS' if ok else ('FAIL' if ok is False else 'info')} | {label} | {d} |")
        pathlib.Path(path).write_text('\n'.join(lines) + '\n')
        print(f'{self.name}: {self.fail} failure(s)')
