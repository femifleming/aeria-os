/* the sequencer's data and its on/off, for the parts that come before seqmode.c */
#if defined(AERIAOS) && (defined(__m68k__) || defined(SEQ_HOST))
#include "seq.h"
#define SEQ_UI 1
static uint8_t seq_on;
#endif
