"""AERIA RESO -- waveguide (Karplus) / complex resonator / detuned resonator, one effect, MODE select."""
from remix.schema import (Category, Proof, BusRole, Claims, DspSection, Formatter, Harness,
                          Kind, MenuEntry, ModeView, Module, Param, YBase)

_NOTES = ('C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B')
_PITCH = tuple(f"{_NOTES[(6 + i) % 12]}{1 + (6 + i) // 12}" for i in range(73))   # F#1 .. F#7
_BLANK = Param(b"", 0)

MODULE = Module(
    name="aeria-reso", key="AERIA RESO", kind=Kind.DSP_EFFECT,
    category=Category.TRACK, author="femi", author_url="",
    proof=Proof.RENDER, proof_note="aeria-fx/reso/test.py under dsp_host; not flashed",
    doc="Resonator: Karplus waveguide, complex resonator, detuned allpass waveguide.",
    menu=MenuEntry(fx2_id=0x1e, donor_desc=0x400d58b8,      # PROVISIONAL id
                   abbr=b"RESO", fullname=b"AERIA Reso", build_tag=False),
    params=(
        Param(b"PTCH", 24, count=73, active=True, formatter=Formatter.WIDE_STEPPED,
              labels=_PITCH, doc="resonant pitch in semitones, F#1 (46 Hz) .. F#7 (2.96 kHz)"),
        Param(b"FINE", 64, active=True, formatter=Formatter.BIPOLAR,
              doc="fine tune +-0.5 semitone (64 = none; clamped at F#1)"),
        Param(b"DECY", 80, active=True, formatter=Formatter.PLAIN,
              doc="fall time to -40 dB, 20 ms .. 20 s (log)"),
        Param(b"TONE", 90, active=True, formatter=Formatter.PLAIN,
              doc="WAVE: loop damping (0 dark); CPLX: input lowpass; DTND: loop lowpass"),
        Param(b"DTUN", 40, active=True, formatter=Formatter.PLAIN,
              doc="WAVE/CPLX: right channel up 0..25 cents; DTND: allpass dispersion"),
        Param(b"MIX", 64, active=True, formatter=Formatter.PLAIN,
              doc="dry/wet; 0 = exact dry"),
        Param(b"MODE", 0, count=3, active=True, formatter=Formatter.STEPPED,
              labels=("WAVE", "CPLX", "DTND"), doc="waveguide / complex resonator / detuned"),
        _BLANK, _BLANK, _BLANK, _BLANK, _BLANK,
    ),
    mode_slot=6,
    mode_views=(
        ModeView(mode=0, defaults={0: 24, 2: 80, 3: 90, 4: 40, 5: 64}),
        ModeView(mode=1, defaults={0: 36, 2: 70, 3: 100, 4: 30, 5: 64}),
        ModeView(mode=2, defaults={0: 24, 2: 90, 3: 110, 4: 64, 5: 64}),
    ),
    dsp=DspSection(asm="reso/reso.asm", priority=15,
                   bus_role=BusRole.NONE, ybase=YBase.NEVER,
                   r7_latch_slot=None, gate_label=None),
    claims=Claims(stock_instance_buffer=True, buffer_words=8192),
    harness=Harness(layout_char="7", is_server=False),
    dear={'MODE': 2, 'DECY': 127, 'DTUN': 127, 'MIX': 127},
)
