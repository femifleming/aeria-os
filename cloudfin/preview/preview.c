#include <stdio.h>
#include "../cloudfin.c"
static void dump(const char *path, unsigned char *fr, int scale) {
  FILE *o=fopen(path,"wb"); fprintf(o,"P5 %d %d 255\n",128*scale,64*scale);
  for(int y=0;y<64*scale;y++)for(int x=0;x<128*scale;x++){int X=x/scale,Y=y/scale;const uint32_t *w=(const uint32_t*)fr;
    int on=(w[X*2+(Y<32?1:0)]>>(Y&31))&1; fputc(on?235:20,o);} fclose(o);}
int main(void){ unsigned char fr[1024] __attribute__((aligned(4))); struct modwerk_runtime_api api;
  char p[64];
  for(int t=0;t<400;t++){ module_tick(&api);
    if(t==120||t==260){ for(int i=0;i<1024;i++)fr[i]=0x5a; module_draw(fr); sprintf(p,"preview/day_%d.pgm",t); dump(p,fr,4);} }
  module_key(KEY_YES,1); module_key(KEY_YES,0); module_enc(0,4);
  for(int t=0;t<100;t++) module_tick(&api);
  for(int i=0;i<1024;i++)fr[i]=0x5a; module_draw(fr); dump("preview/night.pgm",fr,4);
  module_key(KEY_FUNC,1); module_key(KEY_NO,1); for(int i=0;i<1024;i++)fr[i]=0x5a; module_draw(fr);
  printf("hidden leaves stock frame untouched: %s\n", fr[0]==0x5a&&fr[1023]==0x5a?"yes":"NO");
  return 0; }
