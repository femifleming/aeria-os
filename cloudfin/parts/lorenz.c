/* ---- the Lorenz attractor (AERIA OS, sadnoise, 12 Nov 2026), after Chloaelis's (Source/DSP.h `Lorenz`,
 * PluginProcessor.cpp `modulate`): integrated at the UI's tick (61 a second) in fixed point -- the main processor
 * has no floating point. x and y each come out as -1000..1000 (Chloaelis: tanh(x / (1.4 sqrt(beta (rho - 1)))),
 * tanh(y / (1.9 ...))); FILT's two cutoffs follow them, and the quad panner can fly a slot along them.
 *   speed 0..127: 0.03 x 2^(9 v/127) attractor time units a second (Chloaelis 0..1, default 0.35)
 *   chaos 0..127: rho = 24.5 + c^2 x 155.5 (just above 24.7 it barely holds a lobe; higher gets wilder)
 *   shape 0..127: beta 0.5 .. 4 (8/3 at 76)
 * The state is Q12 (x, y about +-60, z up to about 300); a product (a>>6)(b>>6) is Q12 again without overflow. */
#if defined(AERIAOS)
static int32_t lz_x = 410, lz_y = 0, lz_z = 20 << 12;
static int lz_out[2];                      /* -1000..1000: x (the left), y (the right) */
static uint32_t lz_seed = 0x10e7u;
static int32_t lz_mul(int32_t a, int32_t b) { return (a >> 6) * (b >> 6); }
static void lz_f(int32_t a, int32_t b, int32_t c, int32_t rho, int32_t beta, int32_t *da, int32_t *db, int32_t *dc)
{
    *da = 10 * (b - a);
    *db = lz_mul(a, rho - c) - b;
    *dc = lz_mul(a, b) - lz_mul(beta, c);
}
/* h in Q16 attractor time units: d x h, Q12 x Q16 -> Q12, kept inside 32 bits */
static int32_t lz_h(int32_t d, int32_t h) { return (d >> 4) * h >> 12; }
static int lz_isqrt(int32_t v) { int32_t r = 0, b = 1 << 30; if (v <= 0) return 0; while (b > v) b >>= 2; while (b) { if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; } return (int)r; }
/* tanh, close enough (a rational fit): u and the result in thousandths */
static int lz_tanh(int32_t u)
{
    if (u > 4000) return 1000;
    if (u < -4000) return -1000;
    int32_t u2 = u * u / 1000;                                    /* thousandths */
    return (int)(u * (27000 + u2) / (27000 + 9 * u2));
}
static void lz_tick(int speed, int chaos, int shape)
{
    int32_t c = chaos * 1000 / 127;                               /* thousandths */
    int32_t rho = (245 << 12) / 10 + (c * c / 1000) * 637;        /* 24.5 + c^2 x 155.5, Q12 (155.5 x 4096 / 1000 = 637) */
    int32_t beta = (int32_t)(2048 + shape * (14 << 12) / (4 * 127));   /* 0.5 .. 4, Q12 */
    /* speed x dt: 0.03 x 2^(9 s) / 61 time units, as Q16 */
    int32_t e = speed * 9 * 256 / 127;                            /* the exponent, Q8 */
    int32_t pw = 1 << (e >> 8); pw = pw + (pw * (e & 255)) / 256 * 7 / 10 + 0;   /* 2^(e/256), a line between octaves (close) */
    int32_t dt = 32 * pw;                                         /* Q16: 0.03 x 65536 / 61 = 32 */
    int n = (int)((dt + 261) / 262); if (n < 1) n = 1; if (n > 80) n = 80;   /* steps of at most 0.004 */
    int32_t h = dt / n;
    for (int i = 0; i < n; i++) {
        int32_t k1x, k1y, k1z, k2x, k2y, k2z, k3x, k3y, k3z, k4x, k4y, k4z;
        lz_f(lz_x, lz_y, lz_z, rho, beta, &k1x, &k1y, &k1z);
        lz_f(lz_x + lz_h(k1x, h / 2), lz_y + lz_h(k1y, h / 2), lz_z + lz_h(k1z, h / 2), rho, beta, &k2x, &k2y, &k2z);
        lz_f(lz_x + lz_h(k2x, h / 2), lz_y + lz_h(k2y, h / 2), lz_z + lz_h(k2z, h / 2), rho, beta, &k3x, &k3y, &k3z);
        lz_f(lz_x + lz_h(k3x, h), lz_y + lz_h(k3y, h), lz_z + lz_h(k3z, h), rho, beta, &k4x, &k4y, &k4z);
        lz_x += lz_h(k1x + 2 * k2x + 2 * k3x + k4x, h) / 6;
        lz_y += lz_h(k1y + 2 * k2y + 2 * k3y + k4y, h) / 6;
        lz_z += lz_h(k1z + 2 * k2z + 2 * k3z + k4z, h) / 6;
    }
    lz_seed = lz_seed * 1103515245u + 12345u;
    lz_x += (int32_t)((lz_seed >> 16) & 15) - 8;                  /* a whisper of noise so it never settles */
    if (lz_x > (400 << 12) || lz_x < -(400 << 12) || lz_y > (400 << 12) || lz_y < -(400 << 12) || lz_z > (900 << 12) || lz_z < -(100 << 12)) { lz_x = 410; lz_y = 0; lz_z = 20 << 12; }
    /* the outputs: tanh(x / (1.4 sqrt(beta (rho - 1)))) */
    int32_t br = lz_mul(beta, rho - 4096);                        /* Q12 */
    int32_t sq = lz_isqrt(br >> 12); if (sq < 1) sq = 1;          /* whole units */
    lz_out[0] = lz_tanh((lz_x >> 2) * 1000 / (1434 * sq / 1));    /* x / (1.4 sq): (x Q12 >> 2) = x x 1024 */
    lz_out[1] = lz_tanh((lz_y >> 2) * 1000 / (1946 * sq / 1));
}
#endif
