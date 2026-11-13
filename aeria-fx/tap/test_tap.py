"""AERIA TAP gate. Run: aeria-fx/head/run.sh python3 -B /t/test_tap.py"""
import sys
sys.path.insert(0, '/h')
import rig as R

rep = R.Report('TAP')
text = (R.pathlib.Path('/t/tap.asm')).read_text()
rep.check('source: labels, no rep, displacements', not R.source_checks(text), R.source_checks(text))
blob, words, sym = R.assemble(text, R.ORG, 'tap')
dis = R.disassemble(blob, R.ORG)
rep.check('assembles; disassembly has no mpysu/macsu', not R.audit(dis),
          f'{len(words)} words')
rep.note('disassembly', ' / '.join(' '.join(l.split()[1:3]) for l in dis))
_, swords, stub = R.assemble(R.KEYSTUB.read_text(), R.STUB, 'keystub')
null_b, null_w, null = R.assemble('nlini:\n        rts\nnlpro:\n        rts\n', 0x2600, 'null')

for own in (1, 2, 5):
    mem = R.WORK / f'tap{own}.mem'
    R.write_mem(mem, (R.ORG, words), (R.STUB, swords), (0x2600, null_w), own=own)
    n = 16 * 2000
    x = R.stereo(R.noise(n, 0.9, own), R.sine(440, 0.99, n))
    worst = 0
    for split in (None, 1, 7, 15):
        extra = ['-split', str(split)] if split else []
        outs, so = R.host(mem, [(sym['init'], sym['proc'])], [x], [[0] * 12], 2000,
                          extra=extra + ['-guard', '-dirty', '0x5a'], tag=f'tap{own}')
        worst = max(worst, max(abs(v) for v in outs[0]))
        if R.strays(so) and any('0 stray' not in l for l in R.strays(so)):
            rep.check(f'T{own} split {split}: guard', False, R.strays(so))
    rep.check(f'T{own}: 0.9 FS noise L / full-scale sine R, splits none/1/7/15, dirty state, guard: output exactly 0',
              worst == 0, f'max |out| {worst} LSB')
# cost: TAP vs an effect that only returns, same image
mem = R.WORK / 'tapc.mem'
R.write_mem(mem, (R.ORG, words), (0x2600, null_w), own=1)
n = 16 * 3000
outs, so = R.host(mem, [(sym['init'], sym['proc'])], [R.stereo(R.noise(n, 0.5))], [[0] * 12], 3000, tag='tapc')
outs2, so2 = R.host(mem, [(null['nlini'], null['nlpro'])], [R.stereo(R.noise(n, 0.5))], [[0] * 12], 3000, tag='nullc')
(c, cm), (c0, c0m) = R.meter(so), R.meter(so2)
rep.note('cost (dsp_host core meter, executed instructions/sample, 16-frame blocks)',
         f'TAP worst block {c:.2f} (mean {cm:.2f}); an rts-only effect {c0:.2f}; TAP minus null {c - c0:.2f}')
outs, so = R.host(mem, [(sym['init'], sym['proc'])], [R.stereo(R.noise(n, 0.5))], [[0] * 12], 3000, extra=['-split', '1'], tag='tapc1')
rep.note('cost with a trig split at frame 1 every block (minus null)', f'{R.meter(so)[0] - c0:.2f} instructions/sample')
rep.save('/t/test_results.md')
