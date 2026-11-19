/* stubs for the OS calls fuzz_os.c predates (13 Nov 2026), with a little bookkeeping the trim test reads */
#include <stdint.h>
int stub_wx_lp = -9, stub_wx_hp = -9, stub_eq_db[4], stub_wipes, stub_reloads;   /* (stub_eq_db: unused since the EQ went) */
void aos_balloon_cloud(int b, int on, int size, int dens, int pos, int spry, int semi, int rndo, int fade, int level) { (void)b; (void)on; (void)size; (void)dens; (void)pos; (void)spry; (void)semi; (void)rndo; (void)fade; (void)level; }
int aos_clock_seconds(void) { return 0; }
void aos_freeze_delay(int on, int sixteenths, int bpm, int fb, int hp, int lp, int send, int wet, int frozen) { (void)on; (void)sixteenths; (void)bpm; (void)fb; (void)hp; (void)lp; (void)send; (void)wet; (void)frozen; }
void aos_limiter(int on) { (void)on; }
int aos_loop_at(void) { return -1; }
void aos_loop_restart(void) {}
static const uint8_t wave0[128];
const uint8_t *aos_loop_wave(void) { return wave0; }
void aos_lorenz(int l, int r) { (void)l; (void)r; }
void aos_master_eq(int band, int freq, int db) { (void)freq; if (band >= 0 && band < 4) stub_eq_db[band] = db; }
void aos_samples_reload(void) { stub_reloads++; }
int aos_restoring(void) { return 0; }
void aos_weather_filters(int lp, int hp) { stub_wx_lp = lp; stub_wx_hp = hp; }
void aos_wipe_all(void) { stub_wipes++; }
