/* AERIA OS: the sequencer's data, shared by the sound side (seq.c) and the UI (cloudfin/parts/seqmode.c).
 * It lives in battery RAM (0x100C0000), so a sequence is still there after the power has been off; a project
 * copies it whole. Bytes only: no alignment, no endianness. (sadnoise, 12 Nov 2026) */
#ifndef AERIA_SEQ_H
#define AERIA_SEQ_H
#include <stdint.h>
#define SQ_TRACKS 8
#define SQ_STEPS 64
#define SQ_PATTERNS 4
#define SQ_LOCKS 3
#define SEQ_BASE 0x100C0000u
#define SEQ_HEAD_SIZE 0x400u
#define SEQ_STEPS_SIZE 0x6000u       /* 4 patterns x 8 tracks x 64 steps x 12 bytes */
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
       TP_DUCK, TP_SLIP, TP_LFOD, TP_LFOR, TP_LFOA, TP_MACH, TP_MCH, TP_MNOT, TP_SHUF, TP_FREE, TP_REV, TP_RVB, TP_OUT, TP_N };   /* OUT: 0 main, 1 cue, 2 both */
#define SQ_TP 32
/* ... and sixteen more (version 2), kept after the first block; as locks they are 33.. */
enum { TP_DIR = 32, TP_ROT, TP_DENS, TP_MODW, TP_FMR, TP_FMI, TP_FMD, TP_RATE, TP_WARP, TP_CHRD, TP_GATE, TP_PROG, TP_Y, TP_CAR, TP_CSD, TP_CAM, TP_ALL = 48 };   /* CAR/CSD/CAM: the cellular automaton (after norns' less-concepts): rule (0 off), seed, mode */   /* Y: with OUT QUAD, front (the main pair) .. back (the cue pair); 0 = the middle of the knob is 64 */   /* CHRD: the notes MIDI gets (sq_chord); GATE: their length in eighths of a step (0: four); PROG: 0 none, else a program change + 1 */
#define SQ_CHORDS 15
enum { SQM_SMPL, SQM_SUB, SQM_FM, SQM_NOIS, SQM_PLUK, SQM_MIDI, SQM_N };   /* MACH (PLUK: a plucked string, Karplus-Strong) */
#define SQ_RATES 11          /* TP_RATE: the track's clock against the tempo's sixteenths; 5 is the same */
typedef struct {
    uint8_t magic[4];                /* 'AESQ' */
    uint8_t ver, mode;               /* 2 (1: before the second block of parameters); 1 while the sequencer is the OS's face */
    uint8_t sync, pcch;              /* MIDI: bit 0 the clock and start/stop are sent, bit 1 they are obeyed; the channel (1..16, 0 none) program changes
                                      * go out on when the pattern changes -- and on which one coming in picks the pattern */
    uint8_t swing, pattern, mutes;   /* 50..80 %; the pattern wanted (it changes at the bar); a bit a muted track */
    uint8_t duck_src, duck_rel;      /* the track that ducks the others (its DUCK is how deep each goes); how fast they come back */
    uint8_t chain_n, chain[8];       /* a chain of patterns (0: none) */
    uint8_t mlen[SQ_PATTERNS];       /* the pattern's own length in steps: when it changes or its chain moves on */
    uint8_t sel, solo;
    uint8_t mscn[16], mscn_ok[2];    /* sixteen mute scenes (a bit a muted track) and which of them hold one */
    uint8_t nin;                     /* MIDI notes coming in on channels 1..8 play tracks 1..8 (note 60 the track's own pitch): 0 yes, 1 no */
    uint8_t curv, camt, cseed;       /* the clock's curve (after norns' clockabout): 0 off, 1 ACCEL, 2 DECEL, 3 ARCH, 4 BULGE, 5 WAVE, 6 RAND; its amount; RAND's seed */
    uint8_t pad[14];
    uint8_t trk[SQ_TRACKS][SQ_TP];
    uint8_t len[SQ_PATTERNS][SQ_TRACKS];   /* each track's length in each pattern, 1..64 */
    uint8_t fx_row[SQ_TRACKS], fx_core[SQ_TRACKS];   /* each track's effect (0 none, else its row in fx_table) and the core it runs on */
    uint8_t fx_knob[SQ_TRACKS][12];        /* its six knobs and its six FUNC-layer knobs */
    uint8_t trk2[SQ_TRACKS][16];           /* the second block of track parameters (TP_DIR..) */
} SqHead;
#define SQH ((SqHead *)SEQ_BASE)
#define SQ_STEP(p, t, n) ((SqStep *)(SEQ_BASE + SEQ_HEAD_SIZE + 12u * (unsigned)((((p) & 3) * 8 + ((t) & 7)) * 64 + ((n) & 63))))
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
static inline uint8_t *sq_tp(int t, int i) { return i < SQ_TP ? &SQH->trk[t & 7][i] : &SQH->trk2[t & 7][(i - SQ_TP) & 15]; }
void aos_seq_defaults(void);         /* a fresh sequence (also when battery RAM holds none) */
void aos_seq_mod(int t, int which, int v);   /* reserved */
int aos_seq_ext(void);               /* 1 while a clock from outside sets the tempo */
int aos_seq_bpm10(void);             /* the tempo it runs at now */
void aos_resample(int slot, int steps);   /* the main output into a sample slot, from the pattern's next start, this many steps long */
int aos_resampling(void);            /* 0 no, 1 waiting for the bar, 2 recording */
void aos_seq_cushion(int big);       /* more of the stream ahead while the card or a project is at work */
void aos_breathe(void);              /* inside a long loop of the UI's: the stream is fed */
#endif
