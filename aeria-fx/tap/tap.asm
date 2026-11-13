; ---------------------------------------------------------------------------
; AERIA TAP -- an FX1 insert that outputs exact digital silence.
;
; Put it on FX1 of a source track (AERIA: T1 SAMPLE, T2 STARS). The base's
; key bus copies every track's audio as it ENTERS FX1 (KEYBUS site 0,
; dsp_fx1_dispatch, P:$4a7 on payload A / P:$29c on payload B, "the
; dispatcher, for every track on every block, before FX1"), so the track's
; audio is already on the bus when this runs; a reader (AERIA HEAD on T3)
; picks it up from there and the track itself adds nothing to the mix.
;
; ABI: r0 = audio block (interleaved stereo, in place), n7 = frames. No
; state is read or written (the package declares one word because the
; loader wants at least one). init writes nothing (r1 is the dispatcher's).
; Labels are `tp` + two digits; no mpy, no rep.
; ---------------------------------------------------------------------------
init:
        rts

proc:
        move    #>$ffffff,m0
        clr     a
        do      n7,>tp01
        move    a,x:(r0)+
        move    a,x:(r0)+
tp01:
        rts
