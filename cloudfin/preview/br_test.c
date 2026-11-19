/* AERIA OS, 15 Nov 2026: the KIT browser in the sequencer -- PLAY / STOP run the transport, a TRACK key leaves it.
 * Build and run: preview/br_test.sh */
#define main shots_main
#include "br_stubs.c"
#undef main
int16_t aos_mod_tp[SQ_TRACKS][TP_ALL], aos_mod_fxk[SQ_TRACKS][6];
static int fails;
#define CHECK(c, what) do { if (!(c)) { printf("FAIL %s (line %d)\n", what, __LINE__); fails++; } else printf("ok   %s\n", what); } while (0)
int main(void)
{
    intro = INTRO_LEN; seq_enter(1); sq_view = SV_GRID; sq_page = SQP_SRC;
    func_held = 1; key(KEY_YES); func_held = 0;
    CHECK(br_mode == BR_SEQ, "FUNC+YES on SRC opens the KIT browser");
    key(KEY_PLAY);
    CHECK(h_play && br_mode == BR_SEQ, "KIT browser: PLAY starts the sequencer, the browser stays");
    key(KEY_STOP);
    CHECK(!h_play && br_mode == BR_SEQ, "KIT browser: STOP stops it");
    key(KEY_DOWN); CHECK(br_mode == BR_SEQ && br_cur == 1, "KIT browser: its own keys still browse");
    key(KEY_TRACK0 + 3);
    CHECK(br_mode == BR_OFF && SQH->sel == 3, "KIT browser: TRACK 4 leaves it and picks track 4");
    key(KEY_PLAY); CHECK(h_play, "after it: PLAY plays");
    seq_enter(0); browser_open(BR_STARS); key(KEY_TRACK0 + 1);
    CHECK(br_mode == BR_STARS, "sky's STARS browser: unchanged (TRACK does not close it)");
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails != 0;
}
