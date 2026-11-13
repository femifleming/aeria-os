/* AERIA OS's new pages, rendered on the host: OUT, GRAIN CLOUD, MARBLES, PONG */
#define PREVIEW 1
#define AERIAOS 1
#include <stdio.h>
#include <stdint.h>
static uint8_t fake_battery[0x3000] __attribute__((aligned(4)));
#define SNAP_BASE ((uintptr_t)fake_battery)
#include "../aeria12.c"
void aos_route(int s, int on, const uint32_t g[4]) { (void)s; (void)on; (void)g; }
void aos_cue_chain(int on) { (void)on; }
void aos_grain_window(const uint32_t w[65]) { (void)w; }
int aos_midi_out(unsigned st, unsigned a, unsigned b) { (void)st; (void)a; (void)b; return 1; }
void aos_pong(int slot, int ptch, int dec, int level) { (void)slot; (void)ptch; (void)dec; (void)level; }
int aos_slot_ready(int slot) { (void)slot; return 1; }
void aos_balloon(int b, int st) { (void)b; (void)st; }
void aos_balloon_at(int a, int b, int c) { (void)a; (void)b; (void)c; }
void aos_star_at(int a, int b, int c, int d, int e, int f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; }
int aos_src_loop_dummy;
int aos_list(const char *p, void (*each)(const char *, int)) { (void)p; each("Drums", 1); each("kick.wav", 0); return 2; }
int aos_slot_load(int k, int s, const char *p) { (void)k; (void)s; (void)p; return 1; }
int aos_slot_status(int k, int s) { (void)k; (void)s; return 0; }
void aos_hear(int on) { (void)on; }
void aos_tap_want(int w, int on) { (void)w; (void)on; }
int aos_meter(int i) { static const int lv[6] = {11, 7, 0, 0, 13, 12}; return lv[i]; }
void aos_shuffle(int16_t *i, int n, uint32_t s) { (void)i; (void)n; (void)s; }
void aos_note_deal(int b, const char *f, uint32_t s) { (void)b; (void)f; (void)s; }
void aos_usb_disk(int on) { (void)on; }
int aos_disk_busy(void) { return 0; }
#define S 3
#define GAP 12
#define COLS 2
#define ROWS 4
static unsigned char fr[1024] __attribute__((aligned(4)));
static unsigned char sheet[ROWS*(64*S+GAP)+GAP][COLS*(128*S+GAP)+GAP];
static int n_shot;
static void run(int n){ struct modwerk_runtime_api api; for(int i=0;i<n;i++){ module_tick(&api); module_fast(16393); if(i%8==0) preview_step++; } }
static void key(int c){ module_key(c,1); module_key(c,0); }
static void shot(void){
  module_draw(fr); const uint32_t *w=(const uint32_t*)fr;
  int ox=GAP+(n_shot%COLS)*(128*S+GAP), oy=GAP+(n_shot/COLS)*(64*S+GAP);
  for(int y=0;y<64*S;y++)for(int x=0;x<128*S;x++){int X=x/S,Y=y/S; int on=(w[X*2+(Y<32?1:0)]>>(Y&31))&1; sheet[oy+y][ox+x]= on?225:28; }
  n_shot++; }
int main(void){
  for(int y=0;y<(int)(sizeof sheet/sizeof sheet[0]);y++)for(int x=0;x<(int)sizeof sheet[0];x++)sheet[y][x]=70;
  preview_live_fx[8+4]=1; preview_live_fx[0+5]=2; preview_live_fx[8+5]=3; preview_live_fx[0+6]=4;
  run(400); key(0x2a);                                   /* CUE: OUT */
  key(0x02); module_enc(0,2); run(2);                    /* slot 3 -> CUE */
  key(0x01); module_enc(0,3); module_enc(3,2); module_enc(5,40); run(60); shot();   /* slot 2 QUAD ORBIT */
  module_key(0x2d,1); run(2); shot(); module_key(0x2d,0);  /* FUNC layer */
  key(0x2a); key(0x29); run(5); shot();                  /* REC: the grain cloud */
  module_enc(0,4); module_enc(1,-30); run(5); shot();    /* another cloud, skewed */
  key(0x29); key(0x35); key(0x35); key(0x35); module_key(0x31,1); module_key(0x31,0); run(200); shot();  /* MIDI x3: MARBLES on */
  key(0x35); key(0x31); key(0x31); key(0x31); key(0x31); run(90); shot();   /* PONG with 4 balls */
  key(0x25); module_enc(0,1); run(3); shot();             /* MIX with the meters */
  FILE *o=fopen("preview/aeria_os_pages.pgm","wb"); fprintf(o,"P5 %d %d 255\n",(int)sizeof sheet[0],(int)(sizeof sheet/sizeof sheet[0]));
  fwrite(sheet,1,sizeof sheet,o); fclose(o); printf("ok %d shots\n", n_shot); return 0; }
