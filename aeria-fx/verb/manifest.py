"""AERIA VERB -- 8-line FDN reverb (fixed-point port of Cloudfin's FDNReverb), the FDN at half rate (22.05 kHz)."""
from remix.schema import (Category, Proof, BusRole, Claims, DspSection, Formatter, Harness,
                          Kind, MenuEntry, Module, Param, YBase)

MODULE = Module(
    name="aeria-verb", key="AERIA VERB", kind=Kind.DSP_EFFECT,
    category=Category.TRACK, author="femi", author_url="",
    proof=Proof.RENDER, proof_note="aeria-fx/verb/test.py under dsp_host; not flashed",
    doc="Verb: 8-line Hadamard FDN at 22.05 kHz (halfband in/out; wet to ~10.5 kHz), modulated Hermite reads, "
        "bipolar DJ-filter TONE.",
    menu=MenuEntry(fx2_id=0x0e, donor_desc=0x400d58b8,      # PROVISIONAL id
                   abbr=b"VERB", fullname=b"AERIA Verb", build_tag=False),
    params=(
        Param(b"DECY", 64, active=True, formatter=Formatter.PLAIN,
              doc="decay: T60 0.3..30 s (log); 64 = 3 s"),
        Param(b"TONE", 64, active=True, formatter=Formatter.PLAIN,
              doc="DJ filter on the reverb input: 64 = HP 500 Hz; left HP -> 50 Hz and "
                  "LP (9.9 kHz at 64) -> 302 Hz; right HP -> 5 kHz"),
        Param(b"MOD", 64, active=True, formatter=Formatter.PLAIN,
              doc="line modulation depth 0..0.56 ms peak (0..12.4 half-rate samples; 64 = 0.28 ms)"),
        Param(),
        Param(),
        Param(b"MIX", 64, active=True, formatter=Formatter.PLAIN,
              doc="dry/wet; 0 = exact dry"),
        *(Param() for _ in range(6)),
    ),
    # verb.asm is the full-size build: a 32,768-word Y ring (m = $7fff, base
    # aligned to 32K) -- NOT the stock 16K FX2 instance buffer. verb_test.asm
    # (RING=test) holds the same lengths in the 15,870-word window. Half rate: about 14.3K words of the
    # ring are used (the region and the claim are unchanged).
    dsp=DspSection(asm="verb/verb.asm", priority=15,
                   bus_role=BusRole.NONE, ybase=YBase.NEVER,
                   r7_latch_slot=None, gate_label=None),
    claims=Claims(stock_instance_buffer=True, buffer_words=32768),
    harness=Harness(layout_char="7", is_server=False),
    dear={'DECY': 127, 'TONE': 64, 'MOD': 127, 'MIX': 127},
)
