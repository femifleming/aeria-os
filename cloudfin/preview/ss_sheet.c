/* Screensaver contact sheet. Build and run: preview/ss_build.sh [index] (inserts parts/screensavers.c
 *   into aeria12.c before the browser part, as preview/aeria_ss.c, compiles this with clang -O1 -w -I preview, runs it and
 *   converts preview/ss_sheet.pgm to preview/ss_sheet.png with sips).
 * Each screensaver is shown twice: a few seconds in, and later (games much later, to see the idle
 * player still going). Then a long soak of every game checks the idle players never stall. */
#define PREVIEW 1
#include <stdio.h>
#include "aeria_ss.c"
#define S 2
#define GAP 10
#define COLS 4
#define ROWS 10
static unsigned char fr[1024] __attribute__((aligned(4)));
static unsigned char sheet[ROWS * (64 * S + GAP) + GAP][COLS * (128 * S + GAP) + GAP];
static int n_shot, big_k = -1;
static void big(const uint32_t *w)
{
    static unsigned char b[64 * 5][128 * 5];
    for (int y = 0; y < 64 * 5; y++)
        for (int x = 0; x < 128 * 5; x++) { int X = x / 5, Y = y / 5; b[y][x] = ((w[X * 2 + (Y < 32 ? 1 : 0)] >> (Y & 31)) & 1) ? 225 : 28; }
    FILE *o = fopen("preview/ss_big.pgm", "wb");
    fprintf(o, "P5 640 320 255\n");
    fwrite(b, 1, sizeof b, o);
    fclose(o);
}
static void shot(void)
{
    F = (uint32_t *)fr;
    ss_draw();
    const uint32_t *w = (const uint32_t *)fr;
    if (n_shot == big_k) big(w);
    int ox = GAP + (n_shot % COLS) * (128 * S + GAP), oy = GAP + (n_shot / COLS) * (64 * S + GAP);
    for (int y = 0; y < 64 * S; y++)
        for (int x = 0; x < 128 * S; x++) {
            int X = x / S, Y = y / S, on = (w[X * 2 + (Y < 32 ? 1 : 0)] >> (Y & 31)) & 1;
            sheet[oy + y][ox + x] = on ? 225 : 28;
        }
    n_shot++;
}
static void run(int n) { for (int i = 0; i < n; i++) ss_tick(); }
int main(int argc, char **argv)
{
    for (int y = 0; y < (int)(sizeof sheet / sizeof sheet[0]); y++)
        for (int x = 0; x < (int)sizeof sheet[0]; x++) sheet[y][x] = 70;
    int only = argc > 1 ? (argv[1][0] - 48) * (argv[1][1] ? 10 : 1) + (argv[1][1] ? argv[1][1] - 48 : 0) : -1;
    if (argc > 2) big_k = argv[2][0] - '0' + (argv[2][1] ? (argv[2][0] - '0') * 9 + argv[2][1] - '0' : 0);
    for (int s = 1; s < SS_COUNT; s++) {
        if (only >= 0 && s != only) continue;
        ss_start(s);
        if (only >= 0) { for (int k = 0; k < 12; k++) { run(k < 4 ? 90 : 400); shot(); } continue; }
        run(200); shot();
        run(s <= SS_BLOCKS ? 5000 : 500); shot();
    }
    /* a human presses a few keys, then lets go: attract must take over again */
    ss_start(SS_BLOCKS); run(60); ss_key(KEY_LEFT, 1); ss_key(KEY_LEFT, 0); ss_enc(0, 2); run(400);
    printf("blocks after human: dead=%d lines=%d\n", ss.bl.dead, ss.bl.lines);
    static const int keys[] = {KEY_UP, KEY_LEFT, KEY_YES, KEY_DOWN, KEY_RIGHT, KEY_YES, KEY_UP, KEY_NO};
    for (int s = SS_PADDLE; s < SS_COUNT; s++) {
        ss_start(s);
        for (int k = 0; k < 40; k++) { int c = keys[k & 7]; ss_key(c, 1); run(7); ss_enc(k & 1 ? 0 : 6, k & 2 ? 2 : -1); run(5); ss_key(c, 0); run(3); }
        run(600);
        F = (uint32_t *)fr; ss_draw();
    }
    printf("human-input pass ok\n");
    /* soak: each game for 20 minutes of idle play */
    for (int s = SS_PADDLE; s <= SS_BLOCKS; s++) {
        ss_start(s);
        int resets = 0, prev = 0;
        for (int i = 0; i < 72000; i++) {
            ss_tick();
            int v = s == SS_PADDLE ? ss.pd.sc[0] + ss.pd.sc[1] : s == SS_BRICKS ? ss.br.lives : s == SS_ROCKS ? ss.rk.lives
                  : ss.bl.lines;
            if (v < prev && s != SS_BRICKS && s != SS_ROCKS) resets++;
            if (s == SS_BRICKS || s == SS_ROCKS) { if (v > prev && i) resets++; }
            prev = v;
        }
        printf("%-8s soak: resets=%d  ", ss_name[s], resets);
        if (s == SS_PADDLE) printf("score %d-%d\n", ss.pd.sc[0], ss.pd.sc[1]);
        if (s == SS_BRICKS) printf("level %d score %d lives %d\n", ss.br.level, ss.br.score, ss.br.lives);
        if (s == SS_ROCKS) printf("wave %d score %d lives %d\n", ss.rk.wave, ss.rk.score, ss.rk.lives);
        if (s == SS_BLOCKS) printf("lines %d\n", ss.bl.lines);
    }
    FILE *o = fopen("preview/ss_sheet.pgm", "wb");
    fprintf(o, "P5 %d %d 255\n", (int)sizeof sheet[0], (int)(sizeof sheet / sizeof sheet[0]));
    fwrite(sheet, 1, sizeof sheet, o);
    fclose(o);
    return 0;
}
