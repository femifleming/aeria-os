"""place an effect row into a chain slot through the keys, then report meters and DSP health: place.py row [trig]"""
import sys,struct,time; sys.path.insert(0,'/Users/femishonugafleming/octa/aeria-os/tools'); import aos, c1dbg
SYM={l.split()[-1]:int(l.split()[0],16) for l in open('/private/tmp/claude-501/-Users-femishonugafleming-octa/a6e8c4e0-16ca-4199-84ec-2e9cf68c9ec3/scratchpad/sym.txt')}
d=aos.dev()
i32=lambda a: struct.unpack('>i',aos.rdc(d,a,4))[0]
def key(c):
    for p in (1,0): d.ctrl_transfer(0x40,0x03,c,p,None,timeout=2000); time.sleep(0.06)
def enc(e,dl): d.ctrl_transfer(0x40,0x04,e,dl&0xFF,None,timeout=2000); time.sleep(0.05)
def meters(t=0.6):
    aos.wrc(d,SYM['meter_max'],bytes(24)); time.sleep(t)
    return ['%05x'%x for x in struct.unpack('>6I',aos.rdc(d,SYM['meter_max'],24))]
def place(row,trig=5):
    key(0x22); key(trig); key(0x2f); time.sleep(0.15)
    for _ in range(40):
        cur=i32(SYM['shelf_row'])
        if cur==row: break
        enc(0,1 if row>cur else -1)
    key(0x31); time.sleep(0.4)
    return i32(SYM['sel_slot']), aos.rdc(d,SYM['slot_row'],7).hex()
if __name__=='__main__':
    row=int(sys.argv[1]); trig=int(sys.argv[2]) if len(sys.argv)>2 else 5
    print('before',meters())
    print('placed: sel_slot, slot rows',place(row,trig))
    print('after ',meters(),meters())
    a=aos.info(d)[1]['dsp_words']; r1=c1dbg.reports(d,0.4).get(0); time.sleep(0.3); r2=c1dbg.reports(d,0.4).get(0)
    v=struct.unpack('>33I',aos.rd(d,0x100F9000,4*33))
    print('dsp_words +%d, core1 alive %s -> %s, timeouts %x'%(aos.info(d)[1]['dsp_words']-a,r1,r2,v[26]))
