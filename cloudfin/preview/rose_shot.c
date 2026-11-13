#define PREVIEW 1
#include <stdio.h>
#include <string.h>
#include "../aeria12.c"
static unsigned char fr[1024] __attribute__((aligned(4)));
int main(void){ struct modwerk_runtime_api api;
  for(int i=0;i<400;i++){ module_tick(&api); }
  strcpy(slots[6].name,"AERIA ROSE"); slots[6].row=3; slots[6].user[2]=5; slot_bypass=0;
  for(int i=0;i<700;i++) module_tick(&api);
  module_draw(fr); const uint32_t *w=(const uint32_t*)fr;
  FILE *o=fopen("preview/rose.pgm","wb"); fprintf(o,"P5 512 256 255\n");
  for(int y=0;y<256;y++)for(int x=0;x<512;x++){int X=x/4,Y=y/4; fputc(((w[X*2+(Y<32?1:0)]>>(Y&31))&1)?225:28,o);} fclose(o); return 0;}
