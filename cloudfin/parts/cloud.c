/* ---- the grain cloud: GRAINS' window drawn as a cloud (AERIA OS, sadnoise, 10 Nov 2026) --------------------------
 * The top edge of the cloud IS the grain window: a grain fades in and out along the cloud's skyline. The clouds
 * are the ones traced from sadnoise's drawing (sky.c's shapes). REC opens it (any page; it shapes every GRAINS).
 *   SHAP  which cloud (1..13)        SKEW  the peak earlier (fast attack) or later (fast release)
 *   PUFF  how much of the cloud's bumps the window keeps (0 a smooth hill .. 127 every bump)
 *   SOFT  smoothing                  EDGE  the fade at both ends (never quite 0: no grain clicks)
 *   FLIP  the cloud mirrored
 * The window goes to the DSP as 65 points (aeria-os/dsp: GRAINS reads it at r7+$80). */
#if defined(AERIAOS)
void aos_grain_window(const uint32_t w[65]);
typedef struct { uint8_t shape, skew, puff, soft, edge, flip; } CloudSet;
static CloudSet cloud = {0, 64, 64, 30, 12, 0};
static int cloud_open;
static int16_t cloud_w[65];   /* 0..1000 */
static void cloud_compute(void)
{
    const uint8_t *top = shape_top[cloud.shape % N_SHAPES];
    int raw[65], sm[65];
    for (int i = 0; i <= 64; i++) {
        /* SKEW: the time axis bent so the peak moves; then the cloud's top edge read there (40 points, linear) */
        int t = i * 1000 / 64, k = cloud.skew - 64;                      /* -64..63 */
        int u = k >= 0 ? t + (int)((long)k * t * (1000 - t) / (64L * 1000)) : t + (int)((long)k * t * (1000 - t) / (64L * 1000));
        u = clamp(u, 0, 1000);
        if (cloud.flip) u = 1000 - u;
        int pos = u * (SHAPE_N - 1), j = pos / 1000, f = pos % 1000;
        int a = top[j], b = top[j + 1 < SHAPE_N ? j + 1 : j];
        raw[i] = (a * (1000 - f) + b * f) / 1000 * 1000 / 63;          /* 0..1000 */
    }
    /* PUFF: between a smooth hill (the shape's own broad curve) and every bump */
    for (int i = 0; i <= 64; i++) {
        int s = 0, n = 0;
        for (int k = -10; k <= 10; k++) { int j = clamp(i + k, 0, 64); s += raw[j]; n++; }
        int hill = s / n;
        sm[i] = hill + (raw[i] - hill) * cloud.puff / 64;
    }
    /* SOFT: a running average; then normalised to its peak */
    int r = cloud.soft / 16, peak = 1;
    for (int i = 0; i <= 64; i++) {
        int s = 0, n = 0;
        for (int k = -r; k <= r; k++) { int j = clamp(i + k, 0, 64); s += sm[j]; n++; }
        raw[i] = clamp(s / n, 0, 1000);
        if (raw[i] > peak) peak = raw[i];
    }
    /* EDGE: a fade at both ends, at least one point */
    int e = 1 + cloud.edge * 20 / 127;
    for (int i = 0; i <= 64; i++) {
        int v = raw[i] * 1000 / peak;
        int d = i < 64 - i ? i : 64 - i;
        if (d < e) v = v * d / e;
        cloud_w[i] = (int16_t)clamp(v, 0, 1000);
    }
    uint32_t w[65];
    for (int i = 0; i <= 64; i++) w[i] = cloud_w[i] >= 1000 ? 0x7FFFFFu : (uint32_t)cloud_w[i] * 8388u;
    aos_grain_window(w);
}
static void cloud_tick(void) { static CloudSet last = {255, 0, 0, 0, 0, 0}; if (last.shape != cloud.shape || last.skew != cloud.skew || last.puff != cloud.puff || last.soft != cloud.soft || last.edge != cloud.edge || last.flip != cloud.flip) { last = cloud; cloud_compute(); } }
static void cloud_draw(void)
{
    for (int x = 0; x < W; x++) span(x, 9, 63, 0);
    /* the cloud, filled with sky.c's dither, the window its skyline */
    int x0 = 8, x1 = W - 8, base = 50, hgt = 36;
    for (int x = x0; x <= x1; x++) {
        int i = (x - x0) * 64 / (x1 - x0), y = base - cloud_w[i] * hgt / 1000;
        px(x, y); if (x > x0 && x < x1) px(x, y + 1);
        for (int yy = y + 2; yy < base; yy++) if (dither_on(x, yy, 3)) px(x, yy);
        if ((x & 1) == 0) px(x, base + 1);
    }
    /* the cloud's underside, puffed, as sky.c draws them */
    for (int x = x0; x <= x1; x += 1) { int k = (x - x0) * (SHAPE_N - 1) / (x1 - x0); int b = shape_bot[cloud.shape % N_SHAPES][k] * 6 / 63; for (int yy = base; yy <= base + b; yy++) if (dither_on(x, yy, 2)) px(x, yy); }
    const char *lab[6] = {"SHAP", "SKEW", "PUFF", "SOFT", "EDGE", "FLIP"};
    int v[6] = {cloud.shape + 1, cloud.skew, cloud.puff, cloud.soft, cloud.edge, cloud.flip};
    for (int i = 0; i < 6; i++) text(2 + i * 21, 57, lab[i], 1);
    char n[8]; num(n, v[0]); text(W - 2 - text_w(n), 10, n, 1);
}
static void cloud_enc(int e, int d)
{
    int st = d > 0 ? 1 : -1;
    switch (e) {
    case 0: cloud.shape = (uint8_t)((cloud.shape + N_SHAPES + st) % N_SHAPES); break;
    case 1: cloud.skew = (uint8_t)clamp(cloud.skew + d, 0, 127); break;
    case 2: cloud.puff = (uint8_t)clamp(cloud.puff + d, 0, 127); break;
    case 3: cloud.soft = (uint8_t)clamp(cloud.soft + d, 0, 127); break;
    case 4: cloud.edge = (uint8_t)clamp(cloud.edge + d, 0, 127); break;
    case 5: cloud.flip = (uint8_t)(st > 0); break;
    }
    static const char *const l[6] = {"CLOUD", "SKEW", "PUFF", "SOFT", "EDGE", "FLIP"};
    int v[6] = {cloud.shape + 1, cloud.skew, cloud.puff, cloud.soft, cloud.edge, cloud.flip};
    static char n[8]; if (e >= 0 && e < 6) { num(n, v[e]); say(l[e], n); }
}
#endif
