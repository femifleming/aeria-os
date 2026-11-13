#define PREVIEW 1
#include <stdio.h>
#include "../aeria12.c"
#define S 3
#define GAP 12
#define COLS 2
#define ROWS 6
static unsigned char fr[1024] __attribute__((aligned(4)));
static unsigned char sheet[ROWS*(64*S+GAP)+GAP][COLS*(128*S+GAP)+GAP];
static int n_shot;
static void run(int n){ struct modwerk_runtime_api api; for(int i=0;i<n;i++){ module_tick(&api); if(i%8==0) preview_step++; } }
static void key(int c){ module_key(c,1); module_key(c,0); }
static void shot(void){
  module_draw(fr); const uint32_t *w=(const uint32_t*)fr;
  int ox=GAP+(n_shot%COLS)*(128*S+GAP), oy=GAP+(n_shot/COLS)*(64*S+GAP);
  for(int y=0;y<64*S;y++)for(int x=0;x<128*S;x++){int X=x/S,Y=y/S;
    int on=(w[X*2+(Y<32?1:0)]>>(Y&31))&1; sheet[oy+y][ox+x]= on?225:28; }
  n_shot++; }
int main(void){
  for(int t=0;t<8;t++){ for(int i=0x40;i<0x48;i++) preview_pattern[t][50+i]=0xaa; preview_pattern[t][50+0x50]=16;
    for(int s=0;s<64;s++) for(int k=0;k<32;k++) preview_pattern[t][50+0x59+32*s+k]=0xff; }
  /* a chain already on the shelf: T3 FX2 delay, T4 chorus/dark, T5 comb/-, T6 pitch(FX2) */
  preview_live_fx[8+0]=6; preview_live_fx[1]=2; preview_live_fx[8+1]=7; preview_live_fx[2]=3; preview_live_fx[8+3]=9;
  for(int y=0;y<(int)(sizeof sheet/sizeof sheet[0]);y++)for(int x=0;x<(int)sizeof sheet[0];x++)sheet[y][x]=70;
  run(300); shot();                                      /* CHAIN: slot 2 (T4 FX1 chorus) */
  key(0x00); module_enc(1,10); run(20); shot();          /* slot 1: DELAY, FB up */
  key(0x0b); run(5); shot();                             /* the shelf */
  key(0x20); key(0x20); key(0x31); run(5); key(0x03); run(200); key(0x0a); shot(); /* put COMB in, select empty slot 4, RAIN? */
  key(0x24); run(5); shot();                             /* SOURCE */
  module_key(0x2d,1); key(0x00); module_key(0x2d,0); run(5); shot();  /* FUNC+1: LOOP browser */
  key(0x21); run(3); key(0x32); key(0x32); key(0x25); module_enc(0,40); run(10); shot();    /* MIX, FDBK up */
  key(0x26); run(5); shot();                             /* SYSTEM */
  key(0x1e); run(5); shot();                             /* LOOP page */
  key(0x25); key(0x25); module_enc(0,20); module_enc(3,10); run(5); shot();   /* GAIN: IN A up, OUT up */
  key(0x35); key(0x31); key(0x00); key(0x00); key(0x01); key(0x02); key(0x03); key(0x03); run(150); shot(); /* GUMBALLS */
  key(0x26); run(5); key(0x31); run(400); shot();        /* SYSTEM YES: a screensaver */
  printf("IN A lane=%d OUT lane=%d gum notes on=%d\n", preview_lane[HEAD_T][1], preview_lane[LAST_T][15], gum_on); printf("T3 FX2 delay FB lane=%d  HEAD FDBK=%d  T1 vol=%d\n", preview_lane[2][25], preview_lane[2][18], preview_lane[0][15]);
  FILE *o=fopen("preview/aeria12_sheet.pgm","wb"); fprintf(o,"P5 %d %d 255\n",(int)sizeof sheet[0],(int)(sizeof sheet/sizeof sheet[0]));
  fwrite(sheet,1,sizeof sheet,o); fclose(o); return 0; }
