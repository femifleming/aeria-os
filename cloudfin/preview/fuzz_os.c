#define PREVIEW 1
#define AERIAOS 1
#include <stdio.h>
#include <stdint.h>
static uint8_t fake_battery[0x3000] __attribute__((aligned(4)));
#define SNAP_BASE ((uintptr_t)fake_battery)
#include "../aeria12.c"
void aos_route(int s, int on, const uint32_t g[4]) { (void)s; (void)on; (void)g; }
void aos_cue_chain(int on) { (void)on; }
void aos_grain_window(const uint32_t w[65]) { for (int i = 0; i <= 64; i++) if (w[i] > 0x7FFFFFu) { printf("BAD WINDOW %d %x\n", i, w[i]); } }
int aos_midi_out(unsigned st, unsigned a, unsigned b) { if (a > 127 || b > 127 || st < 0x80) printf("BAD MIDI %x %u %u\n", st, a, b); return 1; }
void aos_pong(int slot, int ptch, int dec, int level) { if (slot < 0 || slot > 127 || ptch < 0 || ptch > 127) printf("BAD PONG %d %d\n", slot, ptch); (void)dec; (void)level; }
int aos_slot_ready(int slot) { return slot % 3 != 0; }
void aos_balloon(int b, int st) { (void)b; (void)st; }
void aos_balloon_at(int a, int b, int c) { (void)a; (void)b; (void)c; }
void aos_star_at(int a, int b, int c, int d, int e, int f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; }
int aos_list(const char *p, void (*each)(const char *, int)) { (void)p; each("Drums", 1); each("kick.wav", 0); each("a very long sample file name that goes on and on.wav", 0); return 3; }
int aos_slot_load(int k, int s, const char *p) { (void)k; (void)s; (void)p; return 1; }
int aos_slot_status(int k, int s) { (void)k; (void)s; return 0; }
void aos_hear(int on) { (void)on; }
void aos_shuffle(int16_t *i, int n, uint32_t s) { (void)i; (void)n; (void)s; }
void aos_note_deal(int b, const char *f, uint32_t s) { (void)b; (void)f; (void)s; }
void aos_usb_disk(int on) { (void)on; }
int aos_disk_busy(void) { return 0; }
void aos_tap_want(int w, int on) { (void)w; (void)on; }
int aos_meter(int i) { return i * 3; }
static unsigned char fr[1024] __attribute__((aligned(4)));
static uint32_t r = 12345; static uint32_t rr(void) { r ^= r << 13; r ^= r >> 17; r ^= r << 5; return r; }
int main(void){
  struct modwerk_runtime_api api;
  static const int keys[] = {0x2a, 0x29, 0x35, 0x31, 0x32, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x22, 0x23, 0x24, 0x25, 0x26, 0x1c, 0x1d, 0x1e, 0x1f, 0x1b, 0x20, 0x21, 0x33, 0x34, 0x2d, 0x2b, 0x2c, 0x36, 0x2e, 0x2f};
  for (int t = 0; t < 61 * 3600; t++) {
    module_tick(&api);
    for (int k = 0; k < 64; k++) module_fast(256);
    if (t % 2 == 0) module_draw(fr);
    uint32_t x = rr();
    if ((x & 15) == 0) { int k = keys[(x >> 4) % (sizeof keys / sizeof keys[0])]; if (k == 0x2d) module_key(k, (x >> 12) & 1); else { module_key(k, 1); module_key(k, 0); } }
    if ((x & 7) == 1) module_enc((int)((x >> 8) % 7), (int)((x >> 16) % 21) - 10);
    if ((x & 63) == 2) module_midi(0xB0u | ((x >> 8) & 15), 18 + ((x >> 12) % 20), (x >> 20) & 127);
    if ((x & 255) == 3) module_midi(0xF8, 0, 0);
    if (t % 6100 == 0) preview_step++;
    if (t % 8 == 0) preview_step++;
  }
  printf("fuzz ok: %d simulated seconds\n", 3600); return 0; }
