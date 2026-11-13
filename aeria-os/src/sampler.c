/* AERIA OS: the sample voices. Our own code. WAVs from the card into SDRAM (24-bit, as int32 L R pairs), played by
 * the ColdFire and streamed to the DSP engine's sample ring (engine.asm `stream`): one stereo pair of words per
 * frame, each word the sample >> 1 with bit 23 clear (the engine doubles it back), pushed only while the host port
 * has room, never blocking. The engine reports the frames it has taken (meter index 6); the ColdFire keeps
 * STREAM_AHEAD frames queued (46 ms).
 *
 * Sources (AERIA's sound12.c lanes on its PREVIEW platform):
 *   LOOP   Flex slot 0: loops STRT..END (in 128ths), PTCH (64 = as recorded, a semitone a step), RATE (127 = forward
 *          at speed), ATK fade-in on each pass. Level: HEAD lane 20 (SMPL).
 *   STARS  Static slots bank*16 + planet: a one-shot at the next step with its pitch, attack and decay. Level: HEAD
 *          lane 21 (STAR).
 *   INTRO  /AERIA/INTRO.WAV (if the card has one), once, under the boot logo.
 *   HEAR   Static slot 128: the browser's preview.
 *   BALLOONS  three recorders (A B, C D, the chain's output: engine.asm `record`), sung as a cloud of grains.
 * SDRAM: the loop 0x41000000 (32 MB), the balloons 0x43000000 (3 x 5.3 MB), the intro 0x44000000 (16 MB), stars 0x45000000 (8 banks of 4 MB), the preview
 * 0x47000000 (4 MB). The stack sits at the top (0x47e00000 down). */
#define POOL_LOOP 0x41000000u
#define POOL_BALLOON 0x43000000u
#define POOL_INTRO 0x44000000u
#define POOL_STARS 0x45000000u
#define POOL_HEAR 0x47000000u
#define SZ_LOOP 0x02000000u
#define SZ_BALLOON 0x00550000u   /* each of three: 5.3 MB, 15 s */
#define SZ_INTRO 0x01000000u
#define SZ_BANK 0x00400000u
#define SZ_HEAR 0x00400000u
#define N_STATIC 129
#define STREAM_AHEAD 2048u

typedef struct { int32_t *d; uint32_t frames, step; uint8_t status; } Smp;   /* step: playback step at PTCH 64 (16.16) */
static Smp st_smp[N_STATIC], flex_smp, intro_smp, bal_smp[3];
static uint32_t bank_used[8];

static uint32_t rnd_state = 0x2545F491u;
static uint32_t rnd_u(void) { rnd_state ^= rnd_state << 13; rnd_state ^= rnd_state >> 17; rnd_state ^= rnd_state << 5; return rnd_state; }
static int clamp_i(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static void strcpy_n(char *d, const char *s, int n) { int i = 0; while (s[i] && i < n - 1) { d[i] = s[i]; i++; } d[i] = 0; }
static void strcat_n(char *d, const char *s, int n) { int i = (int)strlen(d); strcpy_n(d + i, s, n - i); }

/* ---- WAV ---- */
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint8_t wav_head[1024] __attribute__((aligned(4)));
static uint32_t div_u(uint32_t n, uint32_t d) { uint32_t q = 0, r = 0; for (int i = 31; i >= 0; i--) { r = r << 1 | ((n >> i) & 1); if (r >= d) { r -= d; q |= 1u << i; } } return q; }
/* path -> s, into [dst, dst+room): 1 if loaded (cut to fit if it is longer) */
static int wav_load(const char *path, Smp *s, uint8_t *dst, uint32_t room)
{
    *(volatile uint32_t *)0x100FBFF0u = 0x70;
    uint32_t cl, size; uint8_t attr;
    s->status = 2; s->frames = 0; s->d = (int32_t *)dst;
    if (!fat_find(path, &cl, &size, &attr) || (attr & 0x10) || size < 44) { s->status = 3; return 0; }
    uint32_t got = fat_read_at(cl, 0, wav_head, sizeof wav_head);
    if (got < 44 || rd32(wav_head) != 0x46464952u || rd32(wav_head + 8) != 0x45564157u) { s->status = 3; return 0; }
    uint32_t fmt = 0, ch = 0, rate = 0, bits = 0, data_at = 0, data_len = 0;
    for (uint32_t o = 12; o + 8 <= got;) {
        uint32_t id = rd32(wav_head + o), len = rd32(wav_head + o + 4);
        if (id == 0x20746d66u && o + 24 <= got) {            /* "fmt " */
            fmt = rd16(wav_head + o + 8); ch = rd16(wav_head + o + 10); rate = rd32(wav_head + o + 12); bits = rd16(wav_head + o + 22);
            if (fmt == 0xFFFE && o + 34 <= got) fmt = rd16(wav_head + o + 32);
        } else if (id == 0x61746164u) { data_at = o + 8; data_len = len; break; }   /* "data" */
        o += 8 + len + (len & 1);
    }
    uint32_t bps = bits / 8, fsz = bps * ch;
    if (!data_at || !(fmt == 1 || (fmt == 3 && bits == 32)) || !ch || ch > 2 || !rate || !(bits == 8 || bits == 16 || bits == 24 || bits == 32)) { s->status = 3; return 0; }
    if (data_at + data_len > size) data_len = size - data_at;
    uint32_t frames = div_u(data_len, fsz), max = room / 8;
    if (frames > max) frames = max;
    /* read the raw frames to the end of the space they will fill, then widen them forwards (never overtaking) */
    uint32_t raw = frames * fsz, out = frames * 8;
    uint8_t *src = dst + (out - raw);
    if (fat_read_at(cl, data_at, src, raw) != raw) { s->status = 3; return 0; }
    int32_t *o = (int32_t *)dst;
    for (uint32_t i = 0; i < frames; i++) {
        int32_t v[2];
        for (uint32_t c = 0; c < ch; c++) {
            const uint8_t *p = src + i * fsz + c * bps;
            int32_t x;
            if (bits == 8) x = ((int32_t)p[0] - 128) << 16;
            else if (bits == 16) x = (int32_t)(int16_t)(uint16_t)(p[0] | p[1] << 8) << 8;
            else if (bits == 24) x = (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24) >> 8;
            else if (fmt == 1) x = (int32_t)rd32(p) >> 8;
            else {                                             /* float: sign, exponent, mantissa -> 24 bits */
                uint32_t f = rd32(p), e = (f >> 23) & 0xFF, m = (f & 0x7FFFFF) | 0x800000;
                int sh = 127 - (int)e;                         /* |x| = m * 2^(e-150); 24-bit full scale = 2^23 */
                int32_t a = e == 0 ? 0 : sh <= 0 ? 0x7FFFFF : sh >= 24 ? 0 : (int32_t)(m >> sh);
                if (a > 0x7FFFFF) a = 0x7FFFFF;
                x = (f >> 31) ? -a : a;
            }
            v[c] = x;
        }
        if (ch == 1) v[1] = v[0];
        o[2 * i] = v[0]; o[2 * i + 1] = v[1];
    }
    s->d = o; s->frames = frames;
    s->step = div_u(rate, 44100u) << 16 | (div_u((rate % 44100u) << 16, 44100u) & 0xFFFF);
    s->status = 0;
    return 1;
}

/* ---- the voices ---- */
typedef struct {
    const Smp *s;
    uint32_t pos, frac, step, lo, hi;   /* pos/frac 32.16 within the sample, lo/hi the loop (hi 0: one shot) */
    int32_t env, env_inc, hold, env_dec; /* env 0..1<<24 */
    uint32_t delay;                     /* frames of silence before it starts (a grain's place in the cloud) */
    uint8_t bus, on;
    uint8_t gl, gr;                     /* a balloon's grain: its place between left and right (128 = all of that side) */
} Voice;
enum { BUS_LOOP, BUS_STAR, BUS_INTRO, BUS_HEAR, BUS_BALLOON, BUS_PONG };
#define N_VOICES 16
static Voice voice[N_VOICES];
static uint32_t bus_gain[6] = {0, 0, 1u << 15, 1u << 15, 0, 1u << 14};   /* Q15 */

/* 2^(n/12) in 16.16, n = -48..48 */
static uint32_t semis(int n)
{
    static const uint32_t oct[12] = {65536, 69433, 73562, 77936, 82570, 87480, 92682, 98193, 104032, 110218, 116772, 123715};
    int o = 0; while (n < 0) { n += 12; o--; } while (n >= 12) { n -= 12; o++; }
    uint32_t v = oct[n];
    return o >= 0 ? v << o : v >> -o;
}
static uint32_t mul16(uint32_t a, uint32_t b) { return (a >> 16) * b + (a & 0xFFFF) * (b >> 16) + (((a & 0xFFFF) * (b & 0xFFFF)) >> 16); }
/* the scale (the UI's scales.c): an interval in tenths of a cent -> the nearest degree; free when the scale is OFF */
int aos_scale_snap(int dc);
/* 2^(dc / 12000) in 16.16: the semitones from the table, the rest (0..99.9 cents) as e^x to its third power */
static uint32_t cents_step(int dc)
{
    int st = dc >= 0 ? dc / 1000 : -((-dc + 999) / 1000), r = dc - st * 1000;
    uint32_t x = (uint32_t)r * 37856u / 10000u;                        /* r ln 2 / 12000, 16.16 */
    uint32_t f = 65536u + x + ((x * x) >> 17) + (((x * x) >> 16) * x) / 393216u;
    return mul16(semis(st < -60 ? -60 : st > 48 ? 48 : st), f);
}
/* a pitch knob's semitones as a playback step, in the scale */
static uint32_t pitch_step(int semi) { return cents_step(aos_scale_snap(semi * 1000)); }
/* x (24-bit-ish) times a Q14 / Q15 factor without 64-bit products (the ColdFire's are 32-bit) */
static int32_t mulq12(int32_t x, int32_t f) { return ((x >> 8) * f >> 4) + (((x & 255) * f) >> 12); }
__attribute__((unused)) static int32_t mulq15(int32_t x, int32_t f) { return ((x >> 8) * f >> 7) + (((x & 255) * f) >> 15); }
/* times for 0..127: about v*v/4 ms (0 = instant, 127 = 4 s) as an envelope step per frame */
static int32_t env_step(int v) { uint32_t frames = (uint32_t)(v * v) * 11u + 1u; return (int32_t)div_u(1u << 24, frames); }

/* How many voices may sound at once. Measured on the unit (12 Nov 2026): a voice costs about 1.1 us a sample, so
 * eight take 40 % of the main processor and the stream still keeps 44,100 frames a second; past that it runs dry
 * (gaps in every sample) and the screen and keys crawl. Sending audio to the computer over USB costs as much again,
 * so six while it records. Past the budget a new voice takes the place of the quietest of its own kind. */
static int voice_budget(void);
static int voices_sounding(void) { int n = 0; for (int i = 0; i < N_VOICES; i++) n += voice[i].on; return n; }
static Voice *voice_get(int bus)
{
    Voice *best = 0;
    if (voices_sounding() < voice_budget()) for (int i = 0; i < N_VOICES; i++) if (!voice[i].on) return &voice[i];
    for (int i = 0; i < N_VOICES; i++) if (voice[i].on && voice[i].bus == bus && (!best || voice[i].env < best->env)) best = &voice[i];
    if (!best) for (int i = 0; i < N_VOICES; i++) if (voice[i].on && voice[i].bus != BUS_LOOP && (!best || voice[i].env < best->env)) best = &voice[i];
    return best ? best : &voice[N_VOICES - 1];
}
static void voices_stop(const Smp *s) { for (int i = 0; i < N_VOICES; i++) if (voice[i].s == s) voice[i].on = 0; }
static void voice_one_shot(const Smp *s, int bus, int ptch, int atk, int dec)
{
    if (!s || s->status || !s->frames) return;
    Voice *v = voice_get(bus);
    v->s = s; v->bus = (uint8_t)bus; v->pos = 0; v->frac = 0; v->lo = 0; v->hi = 0;
    v->step = mul16(s->step, pitch_step(ptch - 64));
    v->env = atk ? 0 : 1 << 24; v->env_inc = env_step(atk); v->env_dec = dec >= 127 ? 0 : env_step(dec);
    v->hold = dec >= 127 ? -1 : 0;
    v->delay = 0;
    v->on = 1;
}

/* LOOP: one voice, re-aimed every tick from the lanes */
static Voice *loop_v;
static void loop_update(int on, int strt, int len, int ptch, int rate, int atk)
{
    if (!on || flex_smp.status || !flex_smp.frames) { if (loop_v) { loop_v->on = 0; loop_v = 0; } return; }
    uint32_t n = flex_smp.frames;
    uint32_t lo = div_u(n, 128) * (uint32_t)strt, hi = lo + div_u(n, 128) * (uint32_t)len;
    if (strt + len >= 128 || hi > n) hi = n;
    if (hi <= lo + 64) hi = lo + 64 < n ? lo + 64 : n;
    if (!loop_v || !loop_v->on || loop_v->s != &flex_smp) {
        loop_v = voice_get(BUS_LOOP);
        loop_v->s = &flex_smp; loop_v->bus = BUS_LOOP; loop_v->pos = lo; loop_v->frac = 0;
        loop_v->env = 0; loop_v->hold = -1; loop_v->env_dec = 0; loop_v->delay = 0; loop_v->on = 1;
    }
    loop_v->lo = lo; loop_v->hi = hi;
    if (loop_v->pos < lo || loop_v->pos >= hi) loop_v->pos = lo;
    loop_v->step = mul16(mul16(flex_smp.step, pitch_step(ptch - 64)), div_u((uint32_t)rate << 16, 127));
    loop_v->env_inc = env_step(atk);
}

static int32_t herm(int32_t ym, int32_t y0, int32_t y1, int32_t y2, int32_t f)
{
    if (!f) return y0;
    int32_t c1 = (y1 - ym) >> 1;
    int32_t c2 = ym - y0 - (y0 + (y0 >> 1)) + 2 * y1 - (y2 >> 1);
    int32_t c3 = ((y2 - ym) >> 1) + (y0 - y1) + ((y0 - y1) >> 1);
    return y0 + mulq12(mulq12(mulq12(c3, f) + c2, f) + c1, f);
}
static int32_t mixL[32], mixR[32];
/* The voices, 32 frames at a time. Measured on the unit (12 Nov 2026): the first version -- Hermite and four wide
 * multiplies for every sample of every voice -- could feed eight voices at most before the stream ran dry and the
 * whole OS crawled. So: a voice at its recorded speed copies its samples; the loop and the card's preview keep the
 * 4-point Hermite; the many short voices (grains, stars, pong) take a 2-point line between samples when more than
 * four voices sound; a steady envelope is not recomputed; one multiply a channel for the level (20-bit sample x
 * 12-bit gain). */
__attribute__((optimize("O2"))) static void voices_render(void)
{
    *(volatile uint32_t *)0x100FBFF0u = 0x80;
    for (int i = 0; i < 32; i++) mixL[i] = mixR[i] = 0;
    int sounding = 0;
    for (int k = 0; k < N_VOICES; k++) sounding += voice[k].on;
    for (int k = 0; k < N_VOICES; k++) {
        Voice *v = &voice[k];
        if (!v->on) continue;
        const int32_t *d = v->s->d; const uint32_t n = v->s->frames, g = bus_gain[v->bus];
        const uint32_t hi = v->hi, lo = v->lo, top = hi ? hi : n, bot = hi ? lo : 0;
        const uint32_t si = v->step >> 16, sf = v->step & 0xFFFF;
        const int plain = !sf && !v->frac;                                  /* at the recorded speed: no in-between */
        const int fine = v->bus == BUS_LOOP || v->bus == BUS_HEAR || sounding <= 4;
        uint32_t pos = v->pos, frac = v->frac;
        int32_t env = v->env, e = (int32_t)(((uint32_t)(env >> 9) * g) >> 18);   /* Q12 */
        int i = 0;
        if (v->delay) { uint32_t k2 = v->delay < 32 ? v->delay : 32; v->delay -= k2; i = (int)k2; }
        for (; i < 32; i++) {
            if (pos >= n) { v->on = 0; break; }
            int32_t l, r;
            if (plain) { l = d[2 * pos]; r = d[2 * pos + 1]; }
            else {
                uint32_t p1 = pos + 1 < top ? pos + 1 : (hi ? bot : pos);
                int32_t f = (int32_t)(frac >> 4);                           /* Q12 */
                if (fine) {
                    uint32_t pm = pos > bot ? pos - 1 : (hi ? top - 1 : pos), p2 = p1 + 1 < top ? p1 + 1 : (hi ? bot : p1);
                    l = herm(d[2 * pm], d[2 * pos], d[2 * p1], d[2 * p2], f);
                    r = herm(d[2 * pm + 1], d[2 * pos + 1], d[2 * p1 + 1], d[2 * p2 + 1], f);
                } else {
                    int32_t a0 = d[2 * pos], a1 = d[2 * pos + 1];
                    l = a0 + ((((d[2 * p1] - a0) >> 4) * (f >> 1)) >> 7);
                    r = a1 + ((((d[2 * p1 + 1] - a1) >> 4) * (f >> 1)) >> 7);
                }
            }
            /* the envelope: attack to full, hold (-1 forever; a count: a grain's flat top), then decay to nothing */
            if (v->env_inc && env < (1 << 24) && v->hold != -2) { env += v->env_inc; if (env >= 1 << 24) { env = 1 << 24; v->env_inc = 0; } e = (int32_t)(((uint32_t)(env >> 9) * g) >> 18); }
            else if (v->hold > 0) v->hold--;
            else if (v->hold == 0 && v->env_dec) { env -= v->env_dec; if (env <= 0) { v->on = 0; break; } e = (int32_t)(((uint32_t)(env >> 9) * g) >> 18); }
            if (v->bus == BUS_BALLOON) { mixL[i] += ((((l >> 4) * e) >> 8) * v->gl) >> 7; mixR[i] += ((((r >> 4) * e) >> 8) * v->gr) >> 7; }
            else {
            mixL[i] += ((l >> 4) * e) >> 8;
            mixR[i] += ((r >> 4) * e) >> 8;
            }
            frac += sf; pos += si + (frac >> 16); frac &= 0xFFFF;
            if (hi && pos >= hi) { pos = lo + (pos - hi) % (hi - lo); if (v->bus == BUS_LOOP && v->env_inc) { env = 0; e = 0; } }
        }
        v->pos = pos; v->frac = frac; v->env = env;
    }
}

/* ---- the balloons: the engine's recorder words (bit 23 set: sample >> 1), in the order its header names ---- */
static uint32_t rec_mask, rec_seen_mask, rec_headers;   /* what the ColdFire asked for; what the last header said */
static int rec_ch;
static uint8_t bal_filling[3];
static void usb_tap(int ch, int32_t v);
static void resample_frame(int32_t l, int32_t r);
static int8_t rec_order[12], rec_n;
static int seq_mode;     /* the sequencer (seq.c): its eight tracks are the stream */
static void fz_in(int ch, int32_t v);
/* the sequencer's stems (12 Nov 2026): after a header 0x78xxxx come 256 words, each track's 16 stereo samples after
 * its effect, track by track; with the frame's main pair (the tap's last two words, alone while stems run) they
 * are 16 samples of 18 channels for USB */
static int stems_on, stems_sent;   /* the computer asked for them; the engine was told */
static void usb_stems(const int32_t *blk, const int32_t *main_lr);
static int32_t stem_main[32]; static int stem_main_n;
static void rec_word(uint32_t w)
{
    if (!(w & 0x800000)) {
        if ((w >> 20) == 7) {                     /* a frame's header: which words follow, in the engine's order */
            if (w & 0x80000u) {                 /* the stems begin: 32 words a track with an effect, 16 a track without */
                if (stem_have) { usb_stems(stem_blk, stem_main); stem_have = 0; }   /* the frame before came without its main pair: it goes with the last one */
                int n = 0; for (int t = 0; t < 8; t++) n += (stem_stereo >> t) & 1 ? 32 : 16;
                stem_left = n; stem_at = 0; stem_trk = 0; stem_wide = stem_stereo & 1; stem_main_n = 0; return;
            }
            stem_left = 0;
            uint32_t m = w & 15;
            if (m != rec_seen_mask || !rec_n) {
                rec_seen_mask = m; rec_n = 0;
                for (int b = 0; b < 3; b++) if (m & (1u << b)) { rec_order[rec_n++] = (int8_t)(b * 2); rec_order[rec_n++] = (int8_t)(b * 2 + 1); }
                if (m & 8u) for (int k = stems_sent ? 4 : 0; k < 6; k++) rec_order[rec_n++] = (int8_t)(100 + k);
            }
            rec_ch = 0; rec_headers++;
        }
        return;
    }
    if (!rec_n) return;
    int c = rec_order[rec_ch]; if (++rec_ch == rec_n) rec_ch = 0;
    int32_t v = (int32_t)((w & 0x7FFFFF) << 9) >> 8;
    if (c >= 100 && stems_sent) {                                      /* the main pair beside the stems */
        if (stem_main_n < 32) stem_main[stem_main_n++] = v;
        if (stem_main_n & 1) {} else resample_frame(stem_main[stem_main_n - 2], stem_main[stem_main_n - 1]);
        if (stem_main_n == 32) { if (stem_have) usb_stems(stem_blk, stem_main); stem_have = 0; stem_main_n = 33; }
        return;
    }
    if (c >= 100) { usb_tap(c - 100, v); return; }
    int b = c >> 1;
    if (b == 2 && !seq_mode) fz_in(c & 1, v);                                     /* the chain's output: also the freeze delay's input */
    Smp *s = &bal_smp[b];
    if (!bal_filling[b] || s->frames >= SZ_BALLOON / 8) return;
    s->d[2 * s->frames + (c & 1)] = v;
    if (c & 1) s->frames++;
}
static void dsp_write(uint32_t addr, uint32_t val, int yspace);
/* USB audio streaming (the host chose the six-channel setting): the DSP's tap on or off */
/* the DSP's tap (A B C D, main L R a sample at a time): on while USB audio streams (1) or PITCH listens (2) */
static uint32_t tap_users;
void aos_tap_want(int who, int on)
{
    uint32_t was = tap_users;
    if (on) tap_users |= (uint32_t)who; else tap_users &= ~(uint32_t)who;
    if (!was == !tap_users) return;
    if (tap_users) rec_mask |= 8u; else rec_mask &= ~8u;
    dsp_write(0x2f4, rec_mask, 0);
}
void aos_usb_audio(int on) { aos_tap_want(1, on); }
static int voice_budget(void) { return (tap_users & 1u) ? 6 : 8; }
/* AERIA's balloon b: 1 starts catching (from nothing), 2 stops (it floats and can sing), 0 pops it */
void aos_balloon(int b, int state)
{
    if (b < 0 || b > 2) return;
    Smp *s = &bal_smp[b];
    if (state == 1) {
        voices_stop(s); s->d = (int32_t *)(POOL_BALLOON + SZ_BALLOON * (uint32_t)b); s->frames = 0; s->step = 0x10000; s->status = 2;
        bal_filling[b] = 1; rec_mask |= 1u << b;
    } else {
        bal_filling[b] = 0; rec_mask &= ~(1u << b);
        if (state == 2) s->status = s->frames > 64 ? 0 : 3;
        else { voices_stop(s); s->frames = 0; s->status = 1; }
    }
    dsp_write(0x2f4, rec_mask, 0);
}
/* ---- the balloons' long grains (10 Nov 2026): every floating balloon sings by itself, a cloud of grains read from
 * its own recording in SDRAM (15 s each), so a grain can be seconds long -- the DSP's GRAINS (in the chain, on live
 * sound) remembers 360 ms. As Balloon Shop's sound balloons: frozen grains, octave, random octave, a tuning each.
 *   SIZE 10 ms .. 4 s   DENS how they overlap (sparse .. about four at once)   POS / SPRY where they start
 *   PTCH, OCT, the balloon's own TUNe   RNDO how often a grain jumps an octave   FADE 0 a hill .. 126 a flat top */
typedef struct { uint8_t on, size, dens, pos, spry, rndo, fade; int8_t semi; } BalCloud;
static BalCloud bal_cloud[3];
static int32_t bal_next[3];
static uint32_t bal_level;
void aos_balloon_cloud(int b, int on, int size, int dens, int pos, int spry, int semi, int rndo, int fade, int level)
{
    if (b < 0 || b > 2) return;
    BalCloud *c = &bal_cloud[b];
    c->on = (uint8_t)(on != 0); c->size = (uint8_t)clamp_i(size, 0, 127); c->dens = (uint8_t)clamp_i(dens, 0, 127);
    c->pos = (uint8_t)clamp_i(pos, 0, 127); c->spry = (uint8_t)clamp_i(spry, 0, 127); c->semi = (int8_t)clamp_i(semi, -48, 48);
    c->rndo = (uint8_t)clamp_i(rndo, 0, 127); c->fade = (uint8_t)clamp_i(fade, 0, 126);
    bal_level = (uint32_t)clamp_i(level, 0, 127);
}
static uint32_t bal_glen(const BalCloud *c) { return 441u + (uint32_t)c->size * (uint32_t)c->size * 11u; }   /* frames */
static void balloon_grain(int b, uint32_t delay)
{
    const Smp *s = &bal_smp[b]; const BalCloud *c = &bal_cloud[b];
    int live = 0;
    for (int i = 0; i < N_VOICES; i++) if (voice[i].on && voice[i].s == s) live++;
    if (live >= 4) return;                                               /* about four at once, a balloon */
    if (voices_sounding() >= voice_budget() && live >= 1) return;        /* the budget is spent: this balloon keeps the grain it has */
    uint32_t glen = bal_glen(c);
    int semi = c->semi;
    if (c->rndo && (rnd_u() & 127u) < c->rndo) { uint32_t r = rnd_u() & 3u; semi += r == 0 ? 24 : r == 1 ? -12 : 12; }
    uint32_t step = pitch_step(clamp_i(semi, -48, 48));
    /* the stretch of the balloon it reads; a grain longer than the balloon is cut to fit */
    uint32_t span = ((glen >> 4) * (step >> 4)) >> 8;
    if (span + 64 >= s->frames) {
        uint32_t ratio = div_u((s->frames - 64) << 8, (span >> 8) + 1);  /* 16.16, below 1 */
        if (ratio > 65536u) ratio = 65536u;
        glen = ((glen >> 2) * (ratio >> 2)) >> 12;
        if (glen < 256) glen = 256;
        span = ((glen >> 4) * (step >> 4)) >> 8;
        if (span + 8 >= s->frames) return;
    }
    uint32_t range = s->frames - span - 4;
    int32_t start = (int32_t)(div_u(range, 128) * (uint32_t)c->pos);
    if (c->spry) start += (int32_t)div_u(range, 254) * ((int32_t)(rnd_u() % (uint32_t)(2 * c->spry + 1)) - c->spry);
    if (start < 0) start = 0;
    if ((uint32_t)start > range) start = (int32_t)range;
    uint32_t ramp = glen / 2 - div_u((glen / 2 - 32) * (uint32_t)c->fade, 127);
    if (ramp < 32) ramp = 32;
    Voice *v = voice_get(BUS_BALLOON);
    v->s = s; v->bus = BUS_BALLOON; v->pos = (uint32_t)start; v->frac = 0; v->lo = 0; v->hi = 0;
    v->step = step;
    v->env = 0; v->env_inc = (int32_t)div_u(1u << 24, ramp); if (!v->env_inc) v->env_inc = 1;
    v->env_dec = v->env_inc; v->hold = (int32_t)(glen > 2 * ramp ? glen - 2 * ramp : 0);
    /* each grain somewhere between left and right: SPRY scatters its place in the stereo field as it scatters its
     * place in the recording (0: every grain in the middle .. 127: anywhere, hard left to hard right) */
    { int p = c->spry ? (int)(rnd_u() % (uint32_t)(2 * c->spry + 1)) - c->spry : 0;
      v->gl = (uint8_t)(p > 0 ? 128 - p : 128); v->gr = (uint8_t)(p < 0 ? 128 + p : 128); }
    v->delay = delay;
    v->on = 1;
}
#define TICK_FRAMES 723   /* a UI tick: 1/61 s */
static void balloons_sing(void)
{
    for (int b = 0; b < 3; b++) {
        const Smp *s = &bal_smp[b]; const BalCloud *c = &bal_cloud[b];
        if (!c->on || s->status || s->frames < 512) { bal_next[b] = 0; continue; }
        /* from one grain to the next: DENS 0 four lengths apart .. 24 end to end .. 127 a quarter (four overlap) */
        uint32_t every = div_u(bal_glen(c) * 64u, 16u + 2u * (uint32_t)c->dens);
        if (every < 882) every = 882;                                    /* at most 50 a second */
        for (int k = 0; k < 4 && bal_next[b] < TICK_FRAMES; k++) {
            balloon_grain(b, (uint32_t)(bal_next[b] > 0 ? bal_next[b] : 0));
            bal_next[b] += (int32_t)(every - every / 8 + rnd_u() % (every / 4 + 1));
        }
        bal_next[b] -= TICK_FRAMES;
        if (bal_next[b] < 0) bal_next[b] = 0;
    }
}

/* ---- the master's freeze delay (sadnoise, 12 Nov 2026), after the Octatrack's own --------------------------------
 * On the main mix, after the chain: a ping-pong delay whose time is a number of sixteenth notes at the tempo, with a
 * high-pass and a lowpass in its feedback; FREEZE shuts its input and holds what is in it, round and round, for
 * ever. Its memory is in main memory (6 seconds a side), so it runs HERE: the engine sends the chain's output (the
 * recorder's third source), the delay's wet sound goes back as a second pair in the stream and is added to the main
 * mix before the EQ and the limiter. A change of time fades from the old tap to the new one (no pitch bend); while
 * frozen it simply reads the new length, at once -- the loop jumps, as the Octatrack's did. */
#define FZ_N 262144u                                   /* frames a side */
#define FZ_L ((int32_t *)0x44000000u)                  /* the left line (the intro music's pool: there is none any more) */
#define FZ_R ((int32_t *)(0x44000000u + 4u * FZ_N))
#define FZ_LAG 1895u                                   /* the way out to the ColdFire and back: frames (measured on the unit) */
static struct {
    uint8_t on, frozen, was_on;
    uint32_t w, d, d_old, xf;                          /* write place; the tap, the one it fades from, the fade (0 = done) */
    int32_t fb, wet, send, lp, hp;                     /* Q15 */
    int32_t lpz, hpz;
    int32_t in[2048][2]; uint16_t iw, ir;              /* what the engine has sent and the delay has not yet taken */
} fz;
void aos_freeze_delay(int on, int sixteenths, int bpm, int fb, int hp, int lp, int send, int wet, int frozen)
{
    if (on && !fz.was_on) { for (uint32_t i = 0; i < FZ_N; i++) { FZ_L[i] = 0; FZ_R[i] = 0; } fz.w = 0; fz.iw = fz.ir = 0; fz.lpz = fz.hpz = 0; fz.xf = 0; fz.d = 0; }
    fz.was_on = fz.on = (uint8_t)(on != 0);
    bpm = bpm < 40 ? 40 : bpm > 240 ? 240 : bpm; sixteenths = sixteenths < 1 ? 1 : sixteenths > 16 ? 16 : sixteenths;
    uint32_t d = div_u(661500u * (uint32_t)sixteenths, (uint32_t)bpm);          /* frames: 60 / bpm / 4 x 44,100 */
    if (d > FZ_N - 64) d = FZ_N - 64;
    if (d < FZ_LAG + 16) d = FZ_LAG + 16;                                        /* nothing shorter than the way out and back */
    if (d != fz.d) { if (fz.d && !frozen && !fz.frozen) { fz.d_old = fz.d; fz.xf = 2048; } else fz.xf = 0; fz.d = d; }
    fz.frozen = (uint8_t)(frozen != 0);
    fz.fb = clamp_i(fb, 0, 127) * 250;                                           /* to 0.97 */
    fz.send = clamp_i(send, 0, 127) * 258; fz.wet = clamp_i(wet, 0, 127) * 129;   /* (the wet came back twice the dry's level at 258: measured) */
    { int c = clamp_i(lp, 0, 127); fz.lp = c >= 127 ? 32768 : 600 + c * c * 2; }  /* one-pole coefficients: dark .. open */
    { int c = clamp_i(hp, 0, 127); fz.hp = c * c / 2; }                           /* 0 = none */
    uint32_t want = (rec_mask & ~4u) | (fz.on || bal_filling[2] ? 4u : 0);
    if (want != rec_mask) { rec_mask = want; dsp_write(0x2f4, rec_mask, 0); }
}
static void fz_in(int ch, int32_t v) { if (!fz.on) return; fz.in[fz.iw & 2047][ch] = v; if (ch) fz.iw++; }
static int32_t fzmul(int32_t x, int32_t q15) { return ((x >> 8) * q15) >> 7; }
/* one frame: the wet pair out */
static void fz_frame(int32_t *wl, int32_t *wr)
{
    int32_t in = 0;
    if ((uint16_t)(fz.iw - fz.ir) > 0) { const int32_t *p = fz.in[fz.ir++ & 2047]; in = (p[0] >> 1) + (p[1] >> 1); }
    if ((uint16_t)(fz.iw - fz.ir) > 1536) fz.ir = (uint16_t)(fz.iw - 1024);     /* fallen behind: skip on */
    uint32_t m = FZ_N - 1, w = fz.w, a = (w - fz.d) & m;
    int32_t tl = FZ_L[a], tr = FZ_R[a];
    if (fz.xf) { uint32_t o = (w - fz.d_old) & m; int32_t g = (int32_t)fz.xf * 16; tl += fzmul(FZ_L[o] - tl, g); tr += fzmul(FZ_R[o] - tr, g); fz.xf--; }
    if (fz.frozen) { FZ_L[w] = tr; FZ_R[w] = tl; }
    else {
        int32_t f = tr;
        fz.lpz += fzmul(f - fz.lpz, fz.lp); f = fz.lpz;                          /* the lowpass */
        fz.hpz += fzmul(f - fz.hpz, fz.hp); f -= fz.hpz;                         /* the high-pass */
        int32_t x = fzmul(in, fz.send) + fzmul(f, fz.fb);
        if (x > 0x7FFFFF) x = 0x7FFFFF; else if (x < -0x800000) x = -0x800000;
        FZ_L[w] = x; FZ_R[w] = tl;
    }
    fz.w = (w + 1) & m;
    /* what is heard is read FZ_LAG frames early: the way out and back makes up the rest, so the echoes fall on the beat
     * while the loop itself keeps its full length */
    { uint32_t e = (w - fz.d + FZ_LAG) & m; *wl = fzmul(FZ_L[e], fz.wet); *wr = fzmul(FZ_R[e], fz.wet); }
}

/* ---- the stream to the DSP ---- */
static uint16_t stream_written, stream_taken;
static uint32_t seq_ahead_now;
static uint32_t seq_ahead = 768;   /* samples the sequencer keeps ahead: 768 (17 ms), or 1280 while the card or a project is at work */
static uint32_t stream_slow = 256;   /* how slowly the engine is reading, Q8 (256: at the full rate) */
static uint32_t stream_taken_at;   /* when the engine last said how far it had read */
static uint32_t stream_words[360];
static int stream_fmt;   /* words a frame the engine expects: 2 (the samples) or 8 (the sequencer's tracks), and 2 more with the master return */
static int stream_ret;   /* ... whether the master return is among them */
static int32_t trk_buf[8][32];
static void seq_block(void);
static void seq_tick(void);
static int stream_n, stream_at, stream_gl;   /* ... and how many words of a sequencer sample are still to go */
static int stream_on;
static uint32_t meter_max[8], meter_now[8];
uint32_t vu_now[6];   /* the lamps' own peaks (A B C D L R), let fall by the lamp driver */
/* the level meters (the UI's): 0..16, about 3.5 dB a step from -56 dBFS, falling smoothly (called each UI tick) */
int aos_meter(int i)
{
    if (i < 0 || i > 5) return 0;
    uint32_t v = meter_now[i] << 4;     /* back to 24-bit */
    meter_now[i] -= meter_now[i] / 12;  /* the fall */
    int lv = 0;
    while (v > 0x3F && lv < 16) { v = v * 2 / 3; lv++; }
    return lv;
}
static void rec_word(uint32_t w);
static void stream_meter(uint32_t w)
{
    if ((w & 0x800000) || (w >> 20) == 7) { rec_word(w); return; }
    uint32_t i = w >> 20;
    if (i == 6) {                                                    /* the engine has already shifted it back */
        /* a report comes every 512 samples: 11.6 ms. When the engine is overloaded its frames run late and the
         * reports with them -- then it is fed at the pace it really takes (measured 12 Nov 2026: fed at the full
         * pace while it ran at 111 %, its host ring overflowed, a stream word was taken for an address and it died) */
        uint32_t t = now(), dt = t - stream_taken_at;
        if (dt < MS(100)) { uint32_t q = dt / 5986u; stream_slow = q < 256u ? 256u : q > 1024u ? 1024u : q; }   /* Q8 against 512 x 2993 ticks */
        stream_taken = (uint16_t)(w & 0xFFFF); stream_taken_at = t;
    }
    else if (i < 6) { if ((w & 0xFFFFF) > meter_max[i]) meter_max[i] = w & 0xFFFFF; if ((w & 0xFFFFF) > meter_now[i]) meter_now[i] = w & 0xFFFFF; if ((w & 0xFFFFF) > vu_now[i]) vu_now[i] = w & 0xFFFFF; }
}
static uint32_t sp_dbg[6];   /* bring-up: calls, words, give-ups at a busy port, ticks inside, ticks waiting for the port, longest wait */
static void stream_pump_in(void);
static void stream_pump(void) { uint32_t t0 = now(); sp_dbg[0]++; stream_pump_in(); sp_dbg[3] += now() - t0; }
static void stream_pump_in(void)
{
    if (!stream_on) return;
    REG8(DSP_SELECT) = 0;
    /* paced: the engine takes each word in an interrupt, and a burst after the ColdFire had been away (a screen
     * update: 20 times a second) put 500 of them into one frame -- three quarters of that frame's time gone, an
     * overrun when the chain was full (measured 12 Nov 2026). At most 2.2 times the playing rate, 96 words at once. */
    static uint32_t pace_t, pace_w;
    { uint32_t t = now(), dt = t - pace_t; pace_t = t;
      { volatile uint32_t *dg = (volatile uint32_t *)0x100FBEECu; if (dt > dg[0] && dt < US(2000000)) dg[0] = dt; }   /* bring-up: the longest the pump was away */
      if (dt > US(2000)) dt = US(2000);
      /* stereo: 2.2 times the playing rate (a word every 5.15 us), 48 samples at once. The sequencer: its samples go
       * whole, a header and the tracks' words (11 or 9), at twice the rate and 40 at once: with what a frame's time
       * adds that stays under the engine's 1,024-word ring, which it sorts once a frame */
      static uint32_t pace_acc;
      if (stream_fmt >= 8) { uint32_t g = (uint32_t)stream_fmt + 1u; { uint32_t dv = (1497u * stream_slow) >> 8; pace_acc += dt * g; pace_w += pace_acc / dv; pace_acc %= dv; } if (pace_w > 40u * g) pace_w = 40u * g; }
      else { uint32_t f = stream_fmt ? (uint32_t)stream_fmt : 2u; pace_acc += dt * f; pace_w += pace_acc / 1360u; pace_acc %= 1360u; if (pace_w > 48u * f) pace_w = 48u * f; } }
    for (int guard = 0; guard < 8; guard++) {
        if (!pace_w) return;
        if (stream_at == stream_n) {
            if (stream_fmt >= 8) {
                /* the engine reports its place only now and then: it has read on since, 44.1 samples a millisecond
                 * (at most 560 are assumed, in case it has stopped: with the cushion that stays inside its ring) */
                uint32_t since = (now() - stream_taken_at) / ((2993u * stream_slow) >> 8); if (since > 560u) since = 560u;   /* 132 ticks a microsecond / 44.1 samples a millisecond; a report comes every 512 samples */
                uint32_t ahead = (uint16_t)(stream_written - stream_taken);
                ahead = ahead > since ? ahead - since : 0;
                if (ahead >= seq_ahead) return;
                seq_ahead_now = ahead;   /* 17 ms ahead: a pad sounds at once; the pump is never away 3 ms (measured) */
            } else
            if ((uint16_t)(stream_written - stream_taken) >= STREAM_AHEAD) return;
            { int want = (seq_mode ? 8 : 2) + (fz.on ? 2 : 0);   /* the format changes only when the engine has taken all there was */
              if (!stream_fmt) stream_fmt = 2;
              if (want != stream_fmt) { if (stream_written != stream_taken) return; stream_fmt = want; stream_ret = fz.on; dsp_write(0x37, (fz.on ? 4u : 0u) | (seq_mode ? 8u : 0u) | (stems_on && seq_mode ? 16u : 0u), 0); stems_sent = stems_on && seq_mode; }
              else if ((stems_on && seq_mode) != stems_sent) { stems_sent = stems_on && seq_mode; rec_n = 0; dsp_write(0x37, (stream_ret ? 4u : 0u) | (seq_mode ? 8u : 0u) | (stems_sent ? 16u : 0u), 0); } }
            if (stream_fmt >= 8) {
                { uint32_t t0 = now(); seq_block(); volatile uint32_t *dg = (volatile uint32_t *)0x100FBEE0u; dg[0] = now() - t0; dg[1] = dg[1] + 1; dg[2] = dg[2] + dg[0]; }
                uint32_t *w = stream_words;
                for (int i = 0; i < 32; i++) {
                    *w++ = 0xBFFF00u | (uint32_t)stream_fmt;                 /* the header: this many words follow in one piece */
                    for (int t = 0; t < 8; t++) *w++ = ((uint32_t)trk_buf[t][i] >> 1) & 0x7FFFFF;
                    if (stream_ret) { int32_t wl, wr; fz_frame(&wl, &wr); *w++ = ((uint32_t)wl >> 1) & 0x7FFFFF; *w++ = ((uint32_t)wr >> 1) & 0x7FFFFF; }
                }
            } else {
            { uint32_t t0 = now(); voices_render(); volatile uint32_t *dg = (volatile uint32_t *)0x100FBEE0u; dg[0] = now() - t0; dg[1] = dg[1] + 1; dg[2] = dg[2] + dg[0]; }   /* bring-up: how long a block of voices takes (132 ticks a microsecond) */
            for (int i = 0; i < 32; i++) {
                int32_t l = mixL[i], r = mixR[i];
                if (l > 0x7FFFFF) l = 0x7FFFFF; else if (l < -0x800000) l = -0x800000;
                if (r > 0x7FFFFF) r = 0x7FFFFF; else if (r < -0x800000) r = -0x800000;
                if (stream_ret) {
                    int32_t wl, wr; fz_frame(&wl, &wr);
                    stream_words[4 * i] = ((uint32_t)l >> 1) & 0x7FFFFF; stream_words[4 * i + 1] = ((uint32_t)r >> 1) & 0x7FFFFF;
                    stream_words[4 * i + 2] = ((uint32_t)wl >> 1) & 0x7FFFFF; stream_words[4 * i + 3] = ((uint32_t)wr >> 1) & 0x7FFFFF;
                } else {
                    stream_words[2 * i] = ((uint32_t)l >> 1) & 0x7FFFFF;
                    stream_words[2 * i + 1] = ((uint32_t)r >> 1) & 0x7FFFFF;
                }
            }
            }
            stream_n = 32 * (stream_fmt >= 8 ? stream_fmt + 1 : stream_fmt); stream_at = 0; stream_gl = 0; stream_written = (uint16_t)(stream_written + 32);
        }
        const int grp = stream_fmt >= 8 ? stream_fmt + 1 : 1;
        while (stream_at < stream_n) {
            /* the engine takes a word within a microsecond while it idles between frames, but not at once: giving
             * up at the first busy read sent three words a pass -- 4,000 frames a second of the 44,100, every sample
             * chopped to pieces (found 12 Nov 2026). Wait for it; only a frame being worked on is worth leaving for.
             * A sample of the sequencer's is never left half sent (its header counts the words): it goes in one
             * tight loop. */
            if (!(REG16(HI_ISR) & 0x02)) {
                uint32_t t0 = now();
                while (!(REG16(HI_ISR) & 0x02)) if (now() - t0 > US(40)) { sp_dbg[2]++; return; }
            }
            if (pace_w < (uint32_t)grp) return;
            pace_w -= (uint32_t)grp;
            const uint32_t *wp = &stream_words[stream_at];
            stream_at += grp; sp_dbg[1] += (uint32_t)grp;
            for (int k = 0; k < grp; k++) {
                if (k) { uint32_t t0 = 0; while (!(REG16(HI_ISR) & 0x02)) { if (!t0) t0 = now() | 1u; else if (now() - t0 > US(3000)) return; } }
                uint32_t w = wp[k];
                REG16(HI_TXH) = (uint16_t)((w >> 16) & 0xFF);
                REG16(HI_TXM) = (uint16_t)((w >> 8) & 0xFF);
                REG16(HI_TXL) = (uint16_t)(w & 0xFF);
            }
        }
    }
}
/* the card is slow: keep the stream going between sectors while something loads */
static void card_idle(void) { stream_pump(); }

/* ---- what was loaded, kept in battery RAM 0x100FBA00 (768 bytes): the loop's path, and each star bank's folder
 * with the seed that dealt it (the deal is the same shuffle again, so the same 16 samples come back) ---- */
typedef struct { uint32_t magic; char loop[124]; struct { uint32_t seed; char path[76]; } bank[8]; } Loaded;
typedef char loaded_fits[sizeof(Loaded) <= 0x300 ? 1 : -1];
#define LOADED ((volatile Loaded *)0x100FBA00u)
#define LOADED_MAGIC 0x414F5350u   /* 'AOSP' */
static void loaded_init(void)
{
    if (LOADED->magic == LOADED_MAGIC) return;
    volatile uint8_t *b = (volatile uint8_t *)LOADED;
    for (unsigned i = 0; i < sizeof(Loaded); i++) b[i] = 0;
    LOADED->magic = LOADED_MAGIC;
}
static void vcopy(volatile char *d, const char *s, int n) { int i = 0; while (s[i] && i < n - 1) { d[i] = s[i]; i++; } d[i] = 0; }
static void vread(char *d, const volatile char *s, int n) { int i = 0; while (s[i] && i < n - 1) { d[i] = s[i]; i++; } d[i] = 0; }
/* the deal's shuffle (browser.c calls it too) */
void aos_shuffle(int16_t *idx, int n, uint32_t seed)
{
    if (!seed) return;                       /* seed 0: as listed (the sequencer's kit: the folder's first sixteen, in order) */
    for (int i = n - 1; i > 0; i--) {
        seed = seed * 1664525u + 1013904223u;
        int j = (int)((seed >> 8) % (uint32_t)(i + 1));
        int16_t t = idx[i]; idx[i] = idx[j]; idx[j] = t;
    }
}
void aos_note_deal(int bank, const char *folder, uint32_t seed)
{
    if (bank < 0 || bank > 7) return;
    loaded_init(); LOADED->bank[bank].seed = seed; vcopy(LOADED->bank[bank].path, folder[0] ? folder : "/", 76);
}

/* ---- the loop's shape, for the LOOP page's picture: 128 columns, each the loudest of up to 512 frames spread across
 * its part of the sample (0..255, the whole sample's loudest = 255); made again when another sample is loaded ---- */
static uint8_t loop_wave[128];
static uint32_t loop_wave_of;   /* the frame count it was made for */
const uint8_t *aos_loop_wave(void)
{
    if (flex_smp.status || flex_smp.frames < 128) return 0;
    if (loop_wave_of != flex_smp.frames) {
        uint32_t n = flex_smp.frames, per = n / 128, stride = per / 512 + 1, pk[128], top = 1;
        for (int c = 0; c < 128; c++) {
            uint32_t m = 0;
            for (uint32_t i = (uint32_t)c * per, e = i + per; i < e; i += stride) {
                int32_t l = flex_smp.d[2 * i], r = flex_smp.d[2 * i + 1];
                uint32_t a = (uint32_t)(l < 0 ? -l : l), b = (uint32_t)(r < 0 ? -r : r);
                if (a > m) m = a;
                if (b > m) m = b;
            }
            pk[c] = m; if (m > top) top = m;
        }
        for (int c = 0; c < 128; c++) loop_wave[c] = (uint8_t)div_u(pk[c] >> 8, (top >> 16) + 1);
        for (int c = 0; c < 128; c++) if (pk[c] >= top - (top >> 6)) loop_wave[c] = 255;
        loop_wave_of = n;
    }
    return loop_wave;
}
/* where the loop is playing, 0..127 across the sample; -1 when it is not */
void aos_loop_restart(void) { if (loop_v) { loop_v->on = 0; loop_v = 0; } }   /* the loop starts again from its start at the next tick */
int aos_loop_at(void) { return loop_v && loop_v->on && flex_smp.frames ? (int)div_u(loop_v->pos, flex_smp.frames / 128 + 1) : -1; }

/* ---- what the UI calls (browser.c and sound12.c, AERIAOS) ---- */
int aos_slot_load(int kind, int slot, const char *path)
{
    if (kind == 1) {
        voices_stop(&flex_smp); loop_v = 0;
        int ok = wav_load(path, &flex_smp, (uint8_t *)POOL_LOOP, SZ_LOOP);
        if (ok && strlen(path) < 124) { loaded_init(); vcopy(LOADED->loop, path, 124); }
        return ok;
    }
    if (slot == 128) { voices_stop(&st_smp[128]); return wav_load(path, &st_smp[128], (uint8_t *)POOL_HEAR, SZ_HEAR); }
    if (slot < 0 || slot >= 128) return 0;
    int b = slot / 16;
    if (slot % 16 == 0) bank_used[b] = 0;      /* a deal starts at the bank's first slot: the bank is new */
    voices_stop(&st_smp[slot]);
    uint32_t room = SZ_BANK - bank_used[b];
    if (room < 4096) { st_smp[slot].status = 3; return 0; }
    uint32_t left = 15 - (uint32_t)(slot % 16), share = room > left * 0x10000u ? room - left * 0x10000u : 4096;   /* all but 64 KB for each slot still to come */
    int ok = wav_load(path, &st_smp[slot], (uint8_t *)(POOL_STARS + SZ_BANK * (uint32_t)b + bank_used[b]), share & ~7u);
    if (ok) bank_used[b] += st_smp[slot].frames * 8;
    return ok;
}
int aos_slot_status(int kind, int slot)
{
    const Smp *s = kind ? &flex_smp : slot >= 0 && slot < N_STATIC ? &st_smp[slot] : 0;
    return !s ? 3 : s->d ? s->status : 1;
}
void aos_hear(int on) { voices_stop(&st_smp[128]); if (on) voice_one_shot(&st_smp[128], BUS_HEAR, 64, 0, 127); }
/* PONG (pong.c): a ball hits a wall -- the sample in static slot `slot` at pitch ptch (64 = as recorded), its decay,
 * and the module's level (0..127) on its own bus, so the balls sound with STARS off too */
void aos_pong(int slot, int ptch, int dec, int level)
{
    if (slot < 0 || slot >= 128) return;
    bus_gain[BUS_PONG] = (uint32_t)level * 258u;
    voice_one_shot(&st_smp[slot], BUS_PONG, ptch, 0, dec);
}
int aos_slot_ready(int slot) { return slot >= 0 && slot < 128 && !st_smp[slot].status && st_smp[slot].frames; }
void aos_star(int slot, int ptch, int atk, int dec) { if (slot >= 0 && slot < 128) voice_one_shot(&st_smp[slot], BUS_STAR, ptch, atk, dec); }
int aos_list(const char *path, void (*each)(const char *name, int dir))
{
    uint32_t cl, sz; uint8_t a; char nm[256];
    if (!card_ok || !fat_find(path, &cl, &sz, &a) || !(a & 0x10)) return -1;
    dir_open(cl);
    int n = 0;
    while (dir_next(nm, sizeof nm, &a, &cl, &sz)) {
        if (nm[0] == '.' || (a & 0x02)) continue;     /* dot entries, hidden (macOS ._ files) */
        const char *use = nm;
        if (strlen(nm) > 63) use = dir_short;          /* too long to keep: the 8.3 name opens it */
        each(use, (a & 0x10) != 0); n++;
    }
    return n;
}

/* at start: the loop, then each bank's 16 stars again, one file a tick (the UI keeps moving between them) */
static int restore_at = -1;          /* -1 not begun, 0 the loop, 1 + bank*16 + k a star, 129 done */
/* a project was loaded: the samples its list names are read from the card again (restore_tick, a little at a time) */
void aos_samples_reload(void) { for (int i = 0; i < N_VOICES; i++) if (voice[i].bus != BUS_BALLOON) voice[i].on = 0; loop_v = 0; flex_smp.status = 1; flex_smp.frames = 0; restore_at = 0; }
static char rs_names[16][144];
static int rs_n;
static int restore_names(int bank)
{
    static char path[80]; char nm[256]; uint8_t a; uint32_t c, sz;
    static char all[192][64]; int16_t idx[192]; int n = 0;
    vread(path, LOADED->bank[bank].path, 76);
    if (!path[0] || !fat_find(path, &c, &sz, &a) || !(a & 0x10)) return 0;
    dir_open(c);
    while (n < 192 && dir_next(nm, sizeof nm, &a, &c, &sz)) {
        if (nm[0] == '.' || (a & 0x12)) continue;
        int k = (int)strlen(nm);
        if (k < 4 || nm[k - 4] != '.' || (nm[k - 3] | 32) != 'w' || (nm[k - 2] | 32) != 'a' || (nm[k - 1] | 32) != 'v') continue;
        strcpy_n(all[n], k < 64 ? nm : dir_short, 64); idx[n] = (int16_t)n; n++;
    }
    if (!n) return 0;
    aos_shuffle(idx, n, LOADED->bank[bank].seed);
    for (int k = 0; k < 16; k++) {
        int pl = (int)strlen(path);
        strcpy_n(rs_names[k], path, 144);
        if (pl > 1) strcat_n(rs_names[k], "/", 144);
        strcat_n(rs_names[k], all[idx[k % n]], 144);
    }
    return 1;
}
static void restore_tick(void)
{
    if (restore_at >= 129 || !card_ok) return;
    if (LOADED->magic != LOADED_MAGIC) { restore_at = 129; return; }
    if (restore_at < 0) restore_at = 0;
    if (restore_at == 0) {
        char p[124]; vread(p, LOADED->loop, 124);
        if (p[0]) wav_load(p, &flex_smp, (uint8_t *)POOL_LOOP, SZ_LOOP);
        restore_at = 1; return;
    }
    int bank = (restore_at - 1) / 16, k = (restore_at - 1) % 16;
    if (k == 0) { rs_n = restore_names(bank); if (!rs_n) { restore_at += 16; return; } }
    aos_slot_load(0, bank * 16 + k, rs_names[k]);
    restore_at++;
}

/* stars wait for their step (AERIA places them one step ahead) */
typedef struct { int16_t step, len, slot; uint8_t ptch, atk, dec, on; } StarQ;
static StarQ starq[4];
void aos_star_at(int step, int len, int slot, int ptch, int atk, int dec)
{
    for (int i = 0; i < 4; i++) if (!starq[i].on) { starq[i] = (StarQ){(int16_t)step, (int16_t)len, (int16_t)slot, (uint8_t)ptch, (uint8_t)atk, (uint8_t)dec, 1}; return; }
}
extern uint32_t preview_step;
int aos_src_loop(void);
void aos_balloon_at(int step, int len, int b) { (void)step; (void)len; (void)b; }   /* the balloons sing by themselves now (balloons_sing) */
/* the sound side each UI tick: the lanes AERIA set -> the voices */
extern uint8_t preview_lane[8][72];
static void sampler_tick(void)
{
    const uint8_t *h = preview_lane[4], *t1 = preview_lane[0];
    if (preview_step > 4) restore_tick();       /* after the intro has begun */
    bus_gain[BUS_LOOP] = (uint32_t)h[20] * 258u;    /* 127 -> 1.0 (Q15) */
    bus_gain[BUS_STAR] = (uint32_t)h[21] * 258u;
    bus_gain[BUS_BALLOON] = bal_level * 258u;
    balloons_sing();
    seq_tick();
    int loop_on = t1[15] != 0 && aos_src_loop();
    for (int i = 0; i < 4; i++) if (starq[i].on && (int)(preview_step % (uint32_t)starq[i].len) == starq[i].step) {
        aos_star(starq[i].slot, starq[i].ptch, starq[i].atk, starq[i].dec); starq[i].on = 0;
    }
    loop_update(loop_on, t1[1], t1[2] ? t1[2] : 127, t1[0], t1[3], t1[12]);
}
static char intro_path[128];
static void dev_find_wav(void);
static void intro_start(void)
{
#ifndef INTRO_MUSIC
    return;   /* no intro music (sadnoise, 12 Nov 2026); the fidelity tests build with -DINTRO_MUSIC: they play a known noise through it */
#endif
    if (!card_ok) return;
    const char *p = "/AERIA/INTRO.WAV";
    if (!wav_load(p, &intro_smp, (uint8_t *)POOL_INTRO, SZ_INTRO)) {
#ifdef AERIA_FLASH
        return;
#endif
        dev_find_wav();                          /* bring-up: any WAV in a set's AUDIO folder */
        if (!intro_path[0] || !wav_load(intro_path, &intro_smp, (uint8_t *)POOL_INTRO, SZ_INTRO)) return;
    }
    voice_one_shot(&intro_smp, BUS_INTRO, 64, 0, 127);
}
/* bring-up: the first .wav in /<dir>/AUDIO or one folder below it */
static void dev_find_wav(void)
{
    static char dirs[8][64]; int nd = 0; char nm[256]; uint8_t a; uint32_t c, sz;
    intro_path[0] = 0;
    dir_open(0);
    while (nd < 8 && dir_next(nm, sizeof nm, &a, &c, &sz)) if ((a & 0x10) && nm[0] != '.' && strlen(nm) < 40) { strcpy_n(dirs[nd], nm, 64); nd++; }
    for (int i = 0; i < nd && !intro_path[0]; i++) {
        char p[128]; p[0] = '/'; strcpy_n(p + 1, dirs[i], 60); strcat_n(p, "/AUDIO", 128);
        if (!fat_find(p, &c, &sz, &a) || !(a & 0x10)) continue;
        static char subs[8][64]; int ns = 0;
        dir_open(c);
        while (dir_next(nm, sizeof nm, &a, &c, &sz)) {
            if (nm[0] == '.' || (a & 0x02)) continue;
            int k = (int)strlen(nm);
            if (!(a & 0x10) && k > 4 && (nm[k - 1] | 32) == 'v' && (nm[k - 4]) == '.' && sz > 44) {
                strcpy_n(intro_path, p, 128); strcat_n(intro_path, "/", 128); strcat_n(intro_path, k < 60 ? nm : dir_short, 128); return;
            }
            if ((a & 0x10) && ns < 8 && k < 60) { strcpy_n(subs[ns], nm, 64); ns++; }
        }
        for (int j = 0; j < ns && !intro_path[0]; j++) {
            char q[128]; strcpy_n(q, p, 128); strcat_n(q, "/", 128); strcat_n(q, subs[j], 128);
            if (!fat_find(q, &c, &sz, &a)) continue;
            dir_open(c);
            while (dir_next(nm, sizeof nm, &a, &c, &sz)) {
                int k = (int)strlen(nm);
                if (!(a & 0x12) && nm[0] != '.' && k > 4 && (nm[k - 1] | 32) == 'v' && nm[k - 4] == '.' && sz > 44) {
                    strcpy_n(intro_path, q, 128); strcat_n(intro_path, "/", 128); strcat_n(intro_path, k < 60 ? nm : dir_short, 128); return;
                }
            }
        }
    }
}
