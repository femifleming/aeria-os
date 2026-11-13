"""DIN MIDI self-test: queue note-ons on the OUT port and count what comes back on IN (cable OUT -> IN)."""
import sys,struct,time; sys.path.insert(0,'/Users/femishonugafleming/octa/aeria-os/tools'); import aos
MO_R,MO_W,RING,IN,MI=0x40041b5c,0x40041b5e,0x40041b60,0x40041b58,0x40041b4c
d=aos.dev()
g16=lambda a: struct.unpack('>H',aos.rd(d,a,2))[0]; g32=lambda a: struct.unpack('>I',aos.rd(d,a,4))[0]
in0=g32(IN); r0=g16(MO_R); N=20
for i in range(N):
    w=g16(MO_W)
    for k,b in enumerate((0x90,0x30+i,0x64)): aos.wr(d,RING+(w+k)%1024,bytes([b]))
    aos.wr(d,MO_W,struct.pack('>H',(w+3)&0xffff)); time.sleep(0.03)
time.sleep(0.2)
print('sent bytes (UART took): %d of %d;  messages received on IN: %d of %d; last parsed %s'%((g16(MO_R)-r0)&0xffff,3*N,g32(IN)-in0,N,aos.rd(d,MI,4).hex()))
