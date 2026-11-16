/* the sequencer's data and its on/off, for the parts that come before seqmode.c */
#if defined(AERIAOS) && (defined(__m68k__) || defined(SEQ_HOST))
#include "seq.h"
#define SEQ_UI 1
/* (seq_on is declared in head12.c: the sky's parts ask it too, 13 Nov 2026) */
#endif
