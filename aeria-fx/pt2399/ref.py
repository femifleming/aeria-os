"""Float reference: Chloaelis' PT2399Line (DSP.h ~299-412) and DelayCell (slew mode), line for line, with the tanh
stages made linear (input 1.1 tanh(0.9 x) -> 0.99 x, output 1.3 tanh(y/1.3) -> y, regen 1.05 tanh(f r) -> 1.05 f r).
cap=None is the plugin; cap=3 is the AERIA build's clock law (<= 3 bits a sample, the tap shortening under 333 ms)."""
import math
import random

FS = 44100.0
TWO_PI = 2 * math.pi


class Biquad:
    def __init__(self):
        self.b0, self.b1, self.b2, self.a1, self.a2, self.z1, self.z2 = 1.0, 0, 0, 0, 0, 0.0, 0.0

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


class PT2399Line:
    def __init__(self, time, bits=44000, cap=None, tone_scale=1.0, grit=1.0, seed=3, noise=True):
        self.cap, self.ts, self.grit, self.noise = cap, tone_scale, grit, noise
        rate = bits / time
        self.ram_bits = bits
        if cap is not None and rate > cap * FS:
            rate = cap * FS
        self.tap = int(round(rate * time))           # bits between the head and the writer
        self.mem = [i & 1 for i in range(bits)]
        self.w = 0
        self.r = (self.w - self.tap) % bits
        self.bitRate = rate
        self.phase = self.prevIn = self.held = 0.0
        self.encEst = self.encStep = self.decEst = self.decStep = 0.0
        self.encH = self.decH = 0
        self.inLP, self.o1, self.o2 = Biquad(), Biquad(), Biquad()
        self.dcx = self.dcy = 0.0
        self.dcr = 1 - TWO_PI * 10.0 / FS
        self.wph = 0.0
        self.rng = random.Random(seed)
        self.update()

    def update(self):
        R = self.bitRate
        self.leak = math.exp(-TWO_PI * 25.0 / R)
        self.beta = math.exp(-1.0 / (0.0035 * R))
        isq = 1 / math.sqrt(R)
        self.dMin, self.dMax = 2.6 * isq, 145.0 * isq * self.grit
        bw = min(11500.0, max(2400.0, R * 0.048)) * self.ts
        self.inLP.lowpass(bw, 0.62)
        self.o1.lowpass(bw, 0.54)
        self.o2.lowpass(bw * 1.25, 1.3)
        f = math.fmod(R * 15.55, FS)
        self.winc = (FS - f if f > FS / 2 else f) / FS

    def process(self, x, freeze=False):
        inp = 0.0 if freeze else x
        inp = 0.99 * self.inLP.process(inp)
        self.phase += self.bitRate / FS
        n = int(self.phase)
        self.phase -= n
        if n > 0:
            acc = 0.0
            for k in range(1, n + 1):
                xin = self.prevIn + (inp - self.prevIn) * k / n
                bit = 1 if xin > self.encEst else 0
                self.encH = ((self.encH << 1) | bit) & 7
                self.encStep = self.beta * self.encStep + (1 - self.beta) * (self.dMax if self.encH in (0, 7) else self.dMin)
                self.encEst = self.encEst * self.leak + (self.encStep if bit else -self.encStep)
                ob = self.mem[self.r]
                self.r = (self.r + 1) % self.ram_bits
                if not freeze:
                    self.mem[self.w] = bit
                self.w = (self.w + 1) % self.ram_bits
                self.decH = ((self.decH << 1) | ob) & 7
                self.decStep = self.beta * self.decStep + (1 - self.beta) * (self.dMax if self.decH in (0, 7) else self.dMin)
                self.decEst = self.decEst * self.leak + (self.decStep if ob else -self.decStep)
                acc += self.decEst
            self.held = acc / n
        self.prevIn = inp
        y = self.o2.process(self.o1.process(self.held))
        yd = y - self.dcx + self.dcr * self.dcy
        self.dcx, self.dcy = y, yd
        if self.noise:
            self.wph = (self.wph + self.winc) % 1.0
            yd += (self.rng.random() - 0.5) * 0.00012 + math.sin(TWO_PI * self.wph) * 0.00018
        return yd


def run(x, time, regen=0.0, hpf=False, **kw):
    line = PT2399Line(time, **kw)
    hp = Biquad().highpass(320.0, 0.6)
    fb, out = 0.0, []
    for v in x:
        y = line.process(v + fb)
        f = hp.process(y) if hpf else y
        fb = 1.05 * regen * f
        out.append(y)
    return out
