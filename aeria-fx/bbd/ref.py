"""Float reference: Chloaelis' BBDLine + DelayCell (DSP.h ~421-503, ~860-924), line for line, with the tanh stages made
linear as the owner asked (bucketSat -> its slope 0.9775, the output tanh -> identity, regen 1.05 tanh(f r) -> 1.05 f r).
Used by test.py as the plugin to compare the DSP against. `peak=True` swaps the 4 / 6 ms attack detectors for the
DSP's at-once peaks (everything else as the plugin)."""
import math
import random

FS = 44100.0
TWO_PI = 2 * math.pi


def one_pole(fc):
    return 1 - math.exp(-TWO_PI * fc / FS)


class Biquad:
    def __init__(self):
        self.b0, self.b1, self.b2, self.a1, self.a2, self.z1, self.z2 = 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0

    def lowpass(self, fc, q=0.7071):
        fc = min(max(fc, 10.0), FS * 0.45)
        w = TWO_PI * fc / FS
        c, al = math.cos(w), math.sin(w) / (2 * q)
        n = 1 / (1 + al)
        self.b0 = (1 - c) * 0.5 * n
        self.b1 = (1 - c) * n
        self.b2 = self.b0
        self.a1 = -2 * c * n
        self.a2 = (1 - al) * n
        return self

    def highpass(self, fc, q=0.7071):
        fc = min(max(fc, 5.0), FS * 0.45)
        w = TWO_PI * fc / FS
        c, al = math.cos(w), math.sin(w) / (2 * q)
        n = 1 / (1 + al)
        self.b0 = (1 + c) * 0.5 * n
        self.b1 = -(1 + c) * n
        self.b2 = self.b0
        self.a1 = -2 * c * n
        self.a2 = (1 - al) * n
        return self

    def process(self, x):
        y = self.b0 * x + self.z1
        self.z1 = self.b1 * x - self.a1 * y + self.z2
        self.z2 = self.b2 * x - self.a2 * y
        return y


class DCBlock:
    def __init__(self, fc):
        self.r, self.x1, self.y1 = 1 - TWO_PI * fc / FS, 0.0, 0.0

    def process(self, x):
        y = x - self.x1 + self.r * self.y1
        self.x1, self.y1 = x, y
        return y


class BBDLine:
    K = 4096
    REF = 0.22

    def __init__(self, time, tone_scale=1.0, seed=5, peak=False, noise=True):
        self.bucket = [0.0] * self.K
        self.idx = 0
        self.clk = self.target = self.K / min(1.6, max(0.02, time))
        self.clkA = one_pole(9.0)
        self.tick = 0.0
        self.prevAA = self.held = self.cti = 0.0
        self.parity = 1.0
        self.comp = self.exp = 0.05
        self.cAtt, self.cRel = one_pole(1 / (TWO_PI * 0.004)), one_pole(1 / (TWO_PI * 0.045))
        self.eAtt, self.eRel = one_pole(1 / (TWO_PI * 0.006)), one_pole(1 / (TWO_PI * 0.060))
        self.aa1 = Biquad().lowpass(6800 * tone_scale, 0.54)
        self.aa2 = Biquad().lowpass(6800 * tone_scale, 1.31)
        self.rc1 = Biquad().lowpass(6200 * tone_scale, 0.54)
        self.rc2 = Biquad().lowpass(6200 * tone_scale, 1.31)
        self.dc = DCBlock(9.0)
        self.rng = random.Random(seed)
        self.peak, self.noise = peak, noise

    def set_time(self, sec, jump=False):
        self.target = self.K / min(1.6, max(0.02, sec))
        if jump:
            self.clk = self.target

    def process(self, x, freeze=False):
        self.clk += (self.target - self.clk) * self.clkA
        ax = abs(x)
        if self.peak:
            self.comp = max(ax, self.comp * (1 - self.cRel))
        else:
            self.comp += (ax - self.comp) * (self.cAtt if ax > self.comp else self.cRel)
        g = min(10.0, 1 / math.sqrt(max(self.comp, 0.0005) / self.REF))
        aa = self.aa2.process(self.aa1.process(x * g))
        inc = self.clk / FS
        self.tick += inc
        while self.tick >= 1.0:
            self.tick -= 1.0
            frac = min(1.0, max(0.0, 1 - self.tick / inc))
            v = self.prevAA + (aa - self.prevAA) * frac
            out = self.bucket[self.idx]
            nz = (self.rng.random() - 0.5) * 0.0009 if self.noise else 0.0
            self.bucket[self.idx] = out * 0.99985 if freeze else 0.9775 * v + nz
            self.idx = (self.idx + 1) % self.K
            self.cti += 0.86 * (out - self.cti)
            self.held = self.cti
            self.parity = -self.parity
        self.prevAA = aa
        y = self.rc2.process(self.rc1.process(self.held + (self.parity * 0.0012 if self.noise else 0.0)))
        if self.noise:
            y += (self.rng.random() - 0.5) * 0.0004
        ay = abs(y)
        if self.peak:
            self.exp = max(ay, self.exp * (1 - self.eRel))
        else:
            self.exp += (ay - self.exp) * (self.eAtt if ay > self.exp else self.eRel)
        y *= min(6.0, max(self.exp, 0.0002) / self.REF)
        return self.dc.process(y)


class Cell:
    """DelayCell around one BBDLine, slew mode (the fade mode's second line is not modelled here)."""

    def __init__(self, time, regen=0.0, hpf=False, **kw):
        self.line = BBDLine(time, **kw)
        self.regen, self.hpf, self.fb = regen, hpf, 0.0
        self.hp = Biquad().highpass(320.0, 0.6)

    def process(self, x, freeze=False):
        y = self.line.process(x + self.fb, freeze)
        f = self.hp.process(y) if self.hpf else y
        self.fb = 0.0 if freeze else 1.05 * f * self.regen
        return y


def run(x, time, regen=0.0, **kw):
    c = Cell(time, regen, **kw)
    return [c.process(v) for v in x]
