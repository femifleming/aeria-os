/* ---- the intro: the brush logo, then BY SADNOISE ------------------------------------------------- */
#define INTRO_LEN 270
static void draw_intro(void)
{
    int t = intro, x0 = (W - LOGO_W) / 2;
    int edge = t < 20 ? -10 : (t - 20) * (LOGO_W + 12) / 70;
    int fade = t < 195 ? 16 : t < 250 ? 16 - (t - 195) * 16 / 55 : 0;
    for (int c = 0; c < LOGO_W; c++) {
        int ragged = sine[(c * 5 + t) & 63] / 40 + sine[(c * 13) & 63] / 50;
        if (c > edge + ragged) break;
        uint32_t lo = logo_cols[c][0], hi = logo_cols[c][1];
        if (fade >= 16 && c < edge - 6) { F[(x0 + c) * 2 + 1] |= lo; F[(x0 + c) * 2] |= hi; continue; }
        int lvl = c >= edge - 6 ? 16 - (c - (edge - 6)) * 2 : fade;
        for (int y = 0; y < 64; y++) {
            uint32_t on = y < 32 ? (lo >> y) & 1u : (hi >> (y - 32)) & 1u;
            if (on && dither_on(x0 + c, y, lvl)) px(x0 + c, y);
        }
    }
    if (t > 95 && t < 252) {
        static const char by[] = "BY SADNOISE";
        char shown[sizeof by];
        int n = (t - 95) / 4, i = 0;
        for (; i < n && by[i]; i++) shown[i] = by[i];
        shown[i] = 0;
        text(64 - text_w(by) / 2, LOGO_BOTTOM + 6, shown, 1);
        if (by[i] && (t & 4)) text(64 - text_w(by) / 2 + i * 4, LOGO_BOTTOM + 6, "-", 1);
    }
    if (t > 168 && t < 176) for (int x = 0; x < W; x++) span(x, 0, H - 1, 3);
    if (t >= 200) { int y = H - (t - 200) * (H - WATER_Y) / 70; for (int x = 0; x < W; x++) px(x, y + sine[(x * 3 + t) & 63] / 120); }
}

