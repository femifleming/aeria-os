#define PREVIEW 1
#include <stdio.h>
#include "../aeria.c"
#define S 3
#define GAP 12
#define COLS 2
#define ROWS 5
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
  for(int y=0;y<(int)(sizeof sheet/sizeof sheet[0]);y++)for(int x=0;x<(int)sizeof sheet[0];x++)sheet[y][x]=70;
  run(150); shot();                                   /* the logo */
  run(160); shot();                                   /* CHAIN, FILTER selected */
  key(0x01); module_enc(1,20); run(30); shot();       /* DELAY, FB turned */
  module_key(0x2d,1); key(0x04); module_key(0x2d,0); key(0x0a); run(200); shot(); /* COMB bypassed, RAIN */
  key(0x0b); run(250); key(0x07); shot();             /* STORM, PLATE selected */
  key(0x24); run(10); shot();                          /* ALGO menu */
  key(0x02); run(200); shot();                         /* STARS chosen */
  key(0x25); module_enc(0,1); run(10); shot();         /* MIX, IN AB */
  key(0x26); run(10); shot();                          /* SYSTEM */
  key(0x23); module_enc(0,3); run(10); shot();         /* SKY */
  printf("chain writes: T5 FILTER WDTH=%d  T5 DELAY FB=%d  T7 COMB MIX=%d  T8 PLATE MIX=%d  T5 INAB=%d INCD=%d  T1 VOL=%d T2 VOL=%d\n",
    preview_lane[4][19], preview_lane[4][25], preview_lane[6][23], preview_lane[7][29], preview_lane[4][0], preview_lane[4][3], preview_lane[0][15], preview_lane[1][15]);
  FILE *o=fopen("preview/aeria_sheet.pgm","wb"); fprintf(o,"P5 %d %d 255\n",(int)sizeof sheet[0],(int)(sizeof sheet/sizeof sheet[0]));
  fwrite(sheet,1,sizeof sheet,o); fclose(o); return 0; }
