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
