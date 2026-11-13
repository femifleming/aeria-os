#define PREVIEW 1
#define AERIAOS 1
#include <stdio.h>
#include <stdint.h>
static uint8_t fake_battery[0x3000] __attribute__((aligned(4)));
#define SNAP_BASE ((uintptr_t)fake_battery)
#include "../aeria12.c"
static int n_on[16], n_off[16]; static double t_now; static double first_on[16], last_on[16]; static int notes_seen[128];
void aos_route(int s, int on, const uint32_t g[4]) { (void)s; (void)on; (void)g; }
void aos_cue_chain(int on) { (void)on; }
void aos_grain_window(const uint32_t w[65]) { (void)w; }
int aos_midi_out(unsigned st, unsigned a, unsigned b) { int ch = st & 15; if ((st & 0xF0) == 0x90 && b) { if (!n_on[ch]) first_on[ch] = t_now; last_on[ch] = t_now; n_on[ch]++; notes_seen[a & 127] = 1; } else if ((st & 0xF0) == 0x80) n_off[ch]++; return 1; }
void aos_pong(int slot, int ptch, int dec, int level) { (void)slot; (void)ptch; (void)dec; (void)level; }
int aos_slot_ready(int slot) { (void)slot; return 0; }
void aos_balloon(int b, int st) { (void)b; (void)st; }
void aos_balloon_at(int a, int b, int c) { (void)a; (void)b; (void)c; }
void aos_star_at(int a, int b, int c, int d, int e, int f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; }
int aos_list(const char *p, void (*each)(const char *, int)) { (void)p; (void)each; return 0; }
int aos_slot_load(int k, int s, const char *p) { (void)k; (void)s; (void)p; return 1; }
int aos_slot_status(int k, int s) { (void)k; (void)s; return 0; }
void aos_hear(int on) { (void)on; }
void aos_shuffle(int16_t *i, int n, uint32_t s) { (void)i; (void)n; (void)s; }
void aos_note_deal(int b, const char *f, uint32_t s) { (void)b; (void)f; (void)s; }
void aos_usb_disk(int on) { (void)on; }
int aos_disk_busy(void) { return 0; }
void aos_tap_want(int w, int on) { (void)w; (void)on; }
int aos_meter(int i) { (void)i; return 0; }
static void run_s(double sec){ struct modwerk_runtime_api api; int ticks_n = (int)(sec * 61); for (int i = 0; i < ticks_n; i++) { module_tick(&api); for (int k = 0; k < 64; k++) { module_fast(256); t_now += 256e-6; } } }
static void key(int c){ module_key(c,1); module_key(c,0); }
int main(void){
  run_s(6);
  key(0x35); key(0x35); key(0x35);            /* MIDI page -> MARBLES */
  key(0x31);                                  /* start */
  run_s(10.0);
  int tot = n_on[0] + n_on[1] + n_on[2];
  printf("MARBLES COIN 150 BPM, 10 s: t1 %d t2 %d t3 %d notes (t2 expected ~%d), offs %d %d %d\n", n_on[0], n_on[1], n_on[2], (int)(10 * 150 * 4 / 60.0), n_off[0], n_off[1], n_off[2]);
  if (n_on[1] > 1) printf("  t2 step %.1f ms (expected 100.0)\n", (last_on[1] - first_on[1]) * 1000 / (n_on[1] - 1));
  int distinct = 0; for (int i = 0; i < 128; i++) distinct += notes_seen[i]; printf("  distinct notes %d (total %d)\n", distinct, tot);
  key(0x31);                                  /* stop */
  for (int c = 0; c < 16; c++) n_on[c] = n_off[c] = 0;
  key(0x35);                                  /* PONG */
  module_enc(5, 1);                           /* MODE MIDI */
  key(0x31); key(0x31); key(0x31);            /* 3 balls */
  run_s(10.0);
  printf("PONG 3 balls, MIDI, 10 s: %d notes on ch1, %d offs\n", n_on[0], n_off[0]);
  return 0; }
