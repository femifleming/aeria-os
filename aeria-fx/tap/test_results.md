| result | check | detail |
|---|---|---|
| PASS | source: labels, no rep, displacements | [] |
| PASS | assembles; disassembly has no mpysu/macsu | 9 words |
| info | disassembly | rts ; / move #>$ffffff,m0 / clr a / do n7,>$2008 / move a,x:(r0)+ / move a,x:(r0)+ / rts ; |
| PASS | T1: 0.9 FS noise L / full-scale sine R, splits none/1/7/15, dirty state, guard: output exactly 0 | max |out| 0 LSB |
| PASS | T2: 0.9 FS noise L / full-scale sine R, splits none/1/7/15, dirty state, guard: output exactly 0 | max |out| 0 LSB |
| PASS | T5: 0.9 FS noise L / full-scale sine R, splits none/1/7/15, dirty state, guard: output exactly 0 | max |out| 0 LSB |
| info | cost (dsp_host core meter, executed instructions/sample, 16-frame blocks) | TAP worst block 2.25 (mean 2.25); an rts-only effect 0.06; TAP minus null 2.19 |
| info | cost with a trig split at frame 1 every block (minus null) | 2.44 instructions/sample |
