/* AERIA's own UI (cloudfin/aeria12.c, the same file the Modwerk module is built from) inside AERIA OS: compiled
 * in its PREVIEW platform mode, whose stock calls are plain arrays (preview_lane, preview_leds, preview_xfade...).
 * main.c feeds it keys, encoders and the fader and carries those arrays to the DSP engine, the LEDs and the LCD. */
#define PREVIEW 1
#define AERIAOS 1
#include "../../cloudfin/aeria12.c"
