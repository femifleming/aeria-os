#define PREVIEW 1
#include <stdio.h>
#include "../aeria.c"
static unsigned char fr[1024] __attribute__((aligned(4)));
static void dump(const char *path) {
  FILE *o=fopen(path,"wb"); fprintf(o,"P5 512 256 255\n");
  const uint32_t *w=(const uint32_t*)fr;
  for(int y=0;y<256;y++)for(int x=0;x<512;x++){int X=x/4,Y=y/4; fputc((w[X*2+(Y<32?1:0)]>>(Y&31))&1?235:20,o);} fclose(o);}
static void run(int n){ struct modwerk_runtime_api api; for(int i=0;i<n;i++) module_tick(&api); }
static void shot(const char *name){ char p[64]; for(int i=0;i<1024;i++)fr[i]=0x5a; module_draw(fr); sprintf(p,"preview/a_%s.pgm",name); dump(p); }
static void key(int c){ module_key(c,1); module_key(c,0); }
int main(void){
  run(60); shot("intro1"); run(60); shot("intro2"); run(40); shot("intro3"); run(100);
  shot("sky");
  key(0x02); run(200); shot("rain");
  key(0x03); run(240); key(0x0f); run(2); shot("storm");
  key(0x04); run(250); shot("snow");
  key(0x05); run(200); shot("heat");
  key(0x06); run(200); shot("fog");
  key(0x01); module_key(0x2b,1); module_key(0x2b,0); module_key(0x2c,1); module_key(0x2c,0); run(150);
  module_key(0x2b,1); module_key(0x2b,0); run(120); key(0x1d); module_enc(0,5); shot("balloons");
  key(0x23); module_enc(1,10); shot("grainpage");
  printf("T1 SIZE lane=%d  T5 WDTH=%d  T2 VOL=%d  track=%d  leds0..3=%d %d %d %d\n", preview_lane[0][1], preview_lane[4][19], preview_lane[1][15], preview_track, preview_leds[0],preview_leds[1],preview_leds[2],preview_leds[3]);
  module_key(0x2d,1); module_key(0x32,1); for(int i=0;i<1024;i++)fr[i]=0x5a; module_draw(fr);
  printf("hidden leaves stock frame: %s\n", fr[0]==0x5a&&fr[1023]==0x5a?"yes":"NO");
  return 0; }
