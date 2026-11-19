/* AERIA OS: the sequencer's data, shared by the sound side (seq.c) and the UI (cloudfin/parts/seqmode.c).
 * It lives in battery RAM (0x100C0000), so a sequence is still there after the power has been off; a project
 * copies it whole. Bytes only: no alignment, no endianness. (sadnoise, 12 Nov 2026) */
#ifndef AERIA_SEQ_H
#define AERIA_SEQ_H
#include <stdint.h>
#define SQ_TRACKS 8
#define SQ_STEPS 64
#define SQ_PATTERNS 8                /* A..H (15 Nov 2026: four more; a sequence of before is brought up, its new patterns empty) */
#define SQ_LOCKS 3
#define SEQ_BASE ((uintptr_t)fake_seq)
#define SEQ_HEAD_SIZE 0x400u
#define SEQ_STEPS_SIZE 0xC000u       /* 8 patterns x 8 tracks x 64 steps x 12 bytes */
/* a step: 12 bytes */
typedef struct {
    uint8_t flags, vel;              /* SF_*; 1..127 (0: the default, 100) */
    int8_t micro;                    /* -23..23 forty-eighths of a step, early or late */
    uint8_t prob, cond, ratch;       /* 0..100 % (0: always); a condition (sq_cond_name); 0/1 once, 2..64 a ratchet */
    uint8_t lp[SQ_LOCKS];            /* parameter locks: 0 none, else a track parameter + 1 */
    uint8_t lv[SQ_LOCKS];            /* ... and its value for this step */
} SqStep;
enum { SF_ON = 1, SF_REV = 2 };            /* flags' top four bits: the ratchet's curve, -7..7 (two's complement): its hits
                                              * come faster and faster (a ball coming to rest), or slower and slower */
#define SQ_CURVE(f) ((int)((int8_t)(f)) >> 4)
/* a track's parameters: 32 bytes, each 0..127 */
enum { TP_SMPL, TP_PTCH, TP_STRT, TP_ATK, TP_DEC, TP_VOL, TP_HP, TP_LP, TP_PAN, TP_DLY, TP_DRIV, TP_BITS, TP_PENV, TP_PDEC,
       TP_DUCK, TP_SLIP, TP_LFOD, TP_LFOR, TP_LFOA, TP_MACH, TP_MCH, TP_MNOT, TP_SHUF, TP_FREE, TP_REV, TP_RVB, TP_OUT, TP_MOUT, TP_COUT, TP_HOUT, TP_N };   /* OUT: 0 main, 1 cue, 2 both, 3 QUAD, 4 EACH; with EACH,
       MOUT / COUT / HOUT (the knobs MAIN, CUE, PHNS) each pair's own: 0 OFF, 1 L, 2 R, 3 L+R (mono), 4 ST (15 Nov 2026) */
#define SQ_TP 32
/* ... and sixteen more (version 2), kept after the first block; as locks they are 33.. */
enum { TP_DIR = 32, TP_ROT, TP_DENS, TP_MODW, TP_FMR, TP_FMI, TP_FMD, TP_RATE, TP_WARP, TP_CHRD, TP_GATE, TP_PROG, TP_Y, TP_CAR, TP_CSD, TP_CAM, TP_2END = 48 };
/* ... and a third block (15 Nov 2026, head version 3): HUMT / HUMV humanise the timing and the velocity (0 off .. 127);
 * VLP / VPT how much a softer hit closes the low-pass / lowers the pitch; TIN reserved. Locks on them are 49.. */
enum { TP_HUMT = 48, TP_HUMV, TP_VLP, TP_VPT, TP_TIN, TP_SLCS, TP_SLIC, TP_LEN, TP_FENV, TP_FDEC, TP_WAVE, TP_DTUN, TP_SUB2, TP_EUC, TP_ALL = 64 };
/* (16 Nov 2026) SLCS cuts the sample into that many equal slices, SLIC picks one (a lock a step: chops), LEN plays only that much
 * of it (0 all); FENV / FDEC a decaying envelope on the low-pass (64 none, below closes, above opens); WAVE DTUN SUB2 the SUB
 * machine's wave (sine, triangle, saw, square), a second oscillator's detune and its level; EUC: that many hits spread evenly over
 * the track's length (Euclid; 0 the grid alone) */
#define TP_FXK 64                    /* a lock on an effect knob: TP_FXK + 0..11 (the six knobs, then the six FUNC knobs); as a lock 65..76 */   /* CAR/CSD/CAM: the cellular automaton (after norns' less-concepts): rule (0 off), seed, mode */   /* Y: with OUT QUAD, front (the main pair) .. back (the cue pair); 0 = the middle of the knob is 64 */   /* CHRD: the notes MIDI gets (sq_chord); GATE: their length in eighths of a step (0: four); PROG: 0 none, else a program change + 1 */
#define SQ_CHORDS 15
enum { SQM_SMPL, SQM_SUB, SQM_FM, SQM_NOIS, SQM_PLUK, SQM_MIDI, SQM_THRU, SQM_N };   /* MACH (PLUK: a plucked string, Karplus-Strong; THRU: the inputs' mix, gated by the steps, 15 Nov 2026) */
#define SQ_RATES 11          /* TP_RATE: the track's clock against the tempo's sixteenths; 5 is the same */
typedef struct {
    uint8_t magic[4];                /* 'AESQ' */
    uint8_t ver, mode;               /* 2 (1: before the second block of parameters); 1 while the sequencer is the OS's face */
    uint8_t sync, pcch;              /* MIDI: bit 0 the clock and start/stop are sent, bit 1 they are obeyed; the channel (1..16, 0 none) program changes
                                      * go out on when the pattern changes -- and on which one coming in picks the pattern */
    uint8_t swing, pattern, mutes;   /* 50..80 %; the pattern wanted (it changes at the bar); a bit a muted track */
    uint8_t duck_src, duck_rel;      /* the track that ducks the others (its DUCK is how deep each goes); how fast they come back */
    uint8_t chain_n, chain[8];       /* a chain of patterns (0: none); entries 9..16 in chain2 */
    uint8_t mlen[4];                 /* patterns A..D's own length in steps (when it changes or its chain moves on); E..H in mlen2 */
    uint8_t sel, solo;
    uint8_t mscn[16], mscn_ok[2];    /* sixteen mute scenes (a bit a muted track) and which of them hold one */
    uint8_t nin;                     /* MIDI notes coming in on channels 1..8 play tracks 1..8 (note 60 the track's own pitch): 0 yes, 1 no */
    uint8_t curv, camt, cseed;       /* the clock's curve (after norns' clockabout): 0 off, 1 ACCEL, 2 DECEL, 3 ARCH, 4 BULGE, 5 WAVE, 6 RAND; its amount; RAND's seed */
    uint8_t rv_on, rv_knob[3], rv_lev, rv_ok;   /* (15 Nov 2026) the reverb bus: on; VERB's DECY, TONE, MOD; the return level;
                                      * 1 once they hold values (a sequence from before has 0: aos_seq_rv_defaults) */
    uint8_t pad[8];
    uint8_t trk[SQ_TRACKS][SQ_TP];
    uint8_t len[4][SQ_TRACKS];             /* each track's length in patterns A..D, 1..64 (E..H: len2) */
    uint8_t fx_row[SQ_TRACKS], fx_core[SQ_TRACKS];   /* each track's effect (0 none, else its row in fx_table) and the core it runs on */
    uint8_t fx_knob[SQ_TRACKS][12];        /* its six knobs and its six FUNC-layer knobs */
    uint8_t trk2[SQ_TRACKS][16];           /* the second block of track parameters (TP_DIR..) */
    /* (15 Nov 2026, version 3: appended, so a sequence of before keeps its place) patterns E..H, a chain up to sixteen long,
     * resampling's source, the third block of track parameters */
    uint8_t mlen2[4];                      /* patterns E..H: their lengths */
    uint8_t len2[4][SQ_TRACKS];            /* ... and each track's length in them */
    uint8_t chain2[8];                     /* the chain's entries 9..16 */
    uint8_t rs_src;                        /* what resampling records: 0 the main out, 1 inputs A B, 2 inputs C D */
    uint8_t trk3[SQ_TRACKS][16];           /* the third block (TP_HUMT..) */
    /* (16 Nov 2026) the song: rows of a pattern, a mute scene and how many times; played in order when it is on */
    uint8_t song_n, song_on, song_row, song_pad;
    uint8_t song[32][2];                   /* [0]: pattern (0..7) | (mute scene 1..16, 0 none) << 3; [1]: times 1..64 */
} SqHead;
#define SQ_SONG_MAX 32
#define SONG_PAT(b) ((b) & 7)
#define SONG_SCN(b) ((b) >> 3)
#define SQH ((SqHead *)SEQ_BASE)
/* ---- the MIDI tracks (sadnoise, 15 Nov 2026): eight more tracks that send MIDI only, each on its own channel, beside
 * the eight audio tracks and on the same clock (the pattern, swing, the page of steps). Kept at battery RAM 0x100C8000:
 * a head and their steps -- 8 bytes a step with one parameter lock (an audio step has three), so a project's copy of
 * all four patterns fits 16 KB. ---- */
#define MT_BASE ((uintptr_t)fake_mt)
#define MT_HEAD_SIZE 0x400u
#define MT_STEPS_SIZE 0x8000u        /* 8 patterns x 8 tracks x 64 steps x 8 bytes */
typedef struct {
    uint8_t flags, vel;              /* SF_ON, and the ratchet's curve in the top four bits (as SqStep); 1..127 (0: the default, 100) */
    int8_t micro;                    /* -23..23 forty-eighths of a step */
    uint8_t prob, cond, ratch;       /* as SqStep */
    uint8_t lp, lv;                  /* one parameter lock: 0 none, else a MIDI track parameter + 1, and its value for this step */
} MtStep;
/* a MIDI track's parameters, 0..127 each: CH 0 off, 1..16; NOTE 0..127; VEL 1..127; GATE eighths of a step (0: four);
 * CHRD sq_chord; PROG 0 none, else a program + 1; OCT 0..6 = -3..+3 octaves; CC1 / CC2 0 none, else a controller
 * (1..127) sent with CC1V / CC2V whenever that value changes (a lock on a step sends it there); FREE 1 = the track is
 * not brought back to its first step when the pattern changes */
enum { MP_CH, MP_NOTE, MP_VEL, MP_GATE, MP_CHRD, MP_PROG, MP_OCT, MP_CC1, MP_CC1V, MP_CC2, MP_CC2V, MP_FREE, MP_HUMT, MP_HUMV, MP_N = 16 };
typedef struct {
    uint8_t magic[4];                /* 'AEMT' */
    uint8_t ver, mutes, solo, sel;   /* 2; a bit a muted / soloed track; the track in hand */
    uint8_t len[4][SQ_TRACKS];       /* each track's length in patterns A..D (E..H: len2) */
    uint8_t trk[SQ_TRACKS][MP_N];
    uint8_t trk2[SQ_TRACKS][16];     /* the second block, at the audio tracks' indexes (TP_DIR.. minus SQ_TP): DIR ROT DENS RATE WARP */
    uint8_t pad[16];
    uint8_t len2[4][SQ_TRACKS];      /* (ver 2, 15 Nov 2026) patterns E..H's lengths, appended */
} MtHead;
#define MTH ((MtHead *)MT_BASE)
#define MT_STEP(p, t, n) ((MtStep *)(MT_BASE + MT_HEAD_SIZE + 8u * (unsigned)((((p) & 7) * 8 + ((t) & 7)) * 64 + ((n) & 63))))
#define SQ_ALL (2 * SQ_TRACKS)       /* the audio tracks are 0..7, the MIDI tracks 8..15 on the clock's side */
#define SQ_STEP(p, t, n) ((SqStep *)(SEQ_BASE + SEQ_HEAD_SIZE + 12u * (unsigned)((((p) & 7) * 8 + ((t) & 7)) * 64 + ((n) & 63))))
/* conditions: 0 none; 1..35 A:B (1:2 2:2 1:3 .. 8:8); then FILL, !FILL, 1ST, !1ST, PRE, !PRE, NEI, !NEI */
#define SQ_COND_AB 35
enum { SQC_FILL = SQ_COND_AB + 1, SQC_NFILL, SQC_FIRST, SQC_NFIRST, SQC_PRE, SQC_NPRE, SQC_NEI, SQC_NNEI, SQ_COND_N };   /* NEI: the track above played its last step */
/* the sound side */
void aos_seq_mode(int on);
int aos_seq_is(void);
void aos_seq_play(int on);            /* 1 from the top, 0 stop, 2 on from where it stopped */
int aos_seq_playing(void);
int aos_seq_step(int t);             /* the step a track is on */
int aos_seq_bar(void);               /* the pattern's own step */
int aos_seq_pat(void);               /* the pattern playing */
void aos_seq_hit(int t, int vel, int rec);   /* a pad: the track sounds now (rec: and is written into the pattern) */
void aos_seq_live(int on);
int aos_seq_ca(int t, int n);          /* -1: no automaton on the track; else the cell under step n now (0/1) */            /* live recording is on: notes from MIDI are written too */
void aos_seq_tempo(int bpm10);
int aos_seq_peak(int t);             /* 0..16 */
void aos_seq_fill(int on);
static inline uint8_t *sq_tp(int t, int i) { return i < SQ_TP ? &SQH->trk[t & 7][i] : i < TP_2END ? &SQH->trk2[t & 7][(i - SQ_TP) & 15] : &SQH->trk3[t & 7][(i - TP_2END) & 15]; }
/* a pattern's length, and a track's length in it, for all eight patterns (the four added ones live in the appended fields) */
static inline uint8_t *sq_mlen_p(int p) { return (p & 7) < 4 ? &SQH->mlen[p & 3] : &SQH->mlen2[(p & 7) - 4]; }
static inline uint8_t *sq_len_p(int p, int t) { return (p & 7) < 4 ? &SQH->len[p & 3][t & 7] : &SQH->len2[(p & 7) - 4][t & 7]; }
static inline uint8_t *sq_chain_p(int k) { return (k & 15) < 8 ? &SQH->chain[k & 7] : &SQH->chain2[(k & 15) - 8]; }
static inline uint8_t *mt_len_p(int p, int t) { return (p & 7) < 4 ? &MTH->len[p & 3][t & 7] : &MTH->len2[(p & 7) - 4][t & 7]; }
typedef char sq_head_fits[sizeof(SqHead) <= SEQ_HEAD_SIZE ? 1 : -1];
typedef char mt_head_fits[sizeof(MtHead) <= MT_HEAD_SIZE ? 1 : -1];
void aos_seq_defaults(void);         /* a fresh sequence (also when battery RAM holds none) */
void aos_mt_defaults(void);          /* fresh MIDI tracks: channel 1..8, C3, velocity 100 */
void aos_seq_refresh(void);          /* the sequence changed under the running sequencer (a project loaded): every track set up again, the stream kept */
void aos_mt_hit(int t, int vel, int rec);   /* a MIDI track's pad (rec: written into the pattern) */
int aos_mt_active(int t);            /* 1 while its last note is fresh (the meter) */
int aos_verb_row(void);              /* VERB's row in the effect table (0: none) */
void aos_seq_rv_defaults(void);      /* the reverb bus's settings, when the sequence has none yet */
void aos_seq_mod(int t, int which, int v);   /* reserved */
int aos_seq_ext(void);               /* 1 while a clock from outside sets the tempo */
int aos_seq_bpm10(void);             /* the tempo it runs at now */
int aos_resample(int steps);         /* what RSRC names (main, A B, C D) into the next free slot of bank 5, from the pattern's next start, this many steps long; the slot (-1: no room); again: given up */
int aos_resampling(void);            /* 0 no, 1 waiting for the bar, 2 recording */
int aos_balloon_to_slot(int b);      /* balloon b's recording copied into the next free slot of bank 5: the slot, or -1 */
int aos_slice(int b, uint8_t *starts);   /* balloon b cut at its transients into bank 5 (slots 5.1..): how many slices (0: nothing to cut); starts: their first step numbers are not needed, the slices are 5.1, 5.2 .. in order */
int aos_bank5_free(void);            /* the next free slot of bank 5 (the display's bank 6: slots 80..95), or -1 */
int aos_bank5_take(int frames);      /* the next slot of bank 5 with room for that many frames (the bank begins again when full): the slot, or -1 */
void aos_seq_cushion(int big);       /* more of the stream ahead while the card or a project is at work */
void aos_breathe(void);              /* inside a long loop of the UI's: the stream is fed */
#endif
