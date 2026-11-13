/* ---- the scale (AERIA OS, sadnoise, 12 Nov 2026), after Balloon Shop and Cloudfin (Scales.h) ----------------------
 * One scale for the whole instrument: MIX, FUNC layer, SCAL (OFF = free) and ROOT. Every pitch knob snaps to it:
 * an interval (PITCH's SHFT, the loop's, the balloons', the stars' and PONG's PTCH) goes to the nearest degree of the
 * scale in any octave, as the plugin's shift knob does; a note (RESO's PTCH) goes to the nearest degree above ROOT.
 * The microtonal scales are kept to a tenth of a cent: the sampler plays the exact ratio, PITCH and RESO get the
 * remainder through their FINE; GRAINS has no fine tune and takes the nearest semitone. */
#if defined(AERIAOS)
typedef char scal_knob_fits[N_SCALES == 41 ? 1 : -1];   /* params12.c: MIX SCAL 0..41 */
static int scale_now(void) { return clamp(pv(PG_MIX, 8), 0, N_SCALES); }
/* an interval in tenths of a cent (either sign) -> the nearest degree in any period; *deg its place in the scale (-1: free) */
static int scale_snap_deg(int dc, int *deg)
{
    int sc = scale_now();
    if (!sc) { if (deg) *deg = -1; return dc; }
    const Scale *s = &scale_list[sc - 1];
    int per = s->period, base = dc >= 0 ? dc / per : -((-dc + per - 1) / per);
    int best = 0, bd = 1 << 30, bi = 0;
    for (int p = base - 1; p <= base + 1; p++)
        for (int i = 0; i < s->n; i++) { int c = p * per + s->dc[i], d = iabs(c - dc); if (d < bd) { bd = d; best = c; bi = i; } }
    if (deg) *deg = bi;
    return best;
}
int aos_scale_snap(int dc) { return scale_snap_deg(dc, 0); }
int aos_scale_root(void) { return clamp(pv(PG_MIX, 9), 0, 11); }
static int dc_round(int dc, int unit) { return (dc >= 0 ? dc + unit / 2 : dc - unit / 2) / unit; }
/* what a pitch knob reads as: free, its semitones; in a scale, where it landed -- semitones when it is a whole
 * number of them, else cents -- and the degree's name (5/4, B3, 7/19 ...) */
static void pitch_text(char *n, int dc)
{
    int deg, q = scale_snap_deg(dc, &deg), i = 0;
    if (deg < 0 || q % 1000 == 0) signed_text(n, q / 1000);
    else { signed_text(n, dc_round(q, 10)); while (n[i]) i++; n[i++] = 'C'; n[i] = 0; }
    if (deg >= 0) {
        const char *l = scale_list[scale_now() - 1].lab[deg];
        i = 0; while (n[i]) i++;
        n[i++] = ' '; while (*l && i < 18) n[i++] = *l++; n[i] = 0;
    }
}
/* RESO's note (MIDI) in the scale: the note it lands on and how far off the keyboard's that is (tenths of a cent) */
static int reso_snap(int midi, int *off)
{
    int root = aos_scale_root(), q = aos_scale_snap((midi - root) * 1000) + root * 1000, m = dc_round(q, 1000);
    if (off) *off = q - m * 1000;
    return m;
}
int aos_reso_snap(int midi, int *off) { return reso_snap(midi, off); }
static void reso_text(char *n, int midi)
{
    static const char *const nn[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int off, m = reso_snap(midi, &off), i = 0;
    const char *p = nn[((m % 12) + 12) % 12]; while (*p) n[i++] = *p++;
    num(n + i, m / 12 - 1); while (n[i]) i++;
    if (off) { n[i++] = ' '; signed_text(n + i, dc_round(off, 10)); while (n[i]) i++; n[i++] = 'C'; n[i] = 0; }
}
#endif
#if defined(AERIAOS)
uint8_t preview_scale_key;
void aos_limiter(int on);
void aos_master_eq(int band, int freq, int db);
static void scale_tick(void) { preview_scale_key = (uint8_t)(scale_now() * 12 + aos_scale_root()); aos_limiter(pv(PG_MIX, 10) != 1);   /* and MIX's LIMT: the main out's limiter */
    /* the master EQ (GAIN, FUNC layer) */
    aos_master_eq(0, 37, pv(PG_GAIN, 6) - 13); aos_master_eq(1, pv(PG_GAIN, 10), pv(PG_GAIN, 7) - 13);
    aos_master_eq(2, pv(PG_GAIN, 11), pv(PG_GAIN, 8) - 13); aos_master_eq(3, 105, pv(PG_GAIN, 9) - 13); }
#endif
