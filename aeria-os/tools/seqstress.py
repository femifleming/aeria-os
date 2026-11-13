"""the sequencer's limits: every track pitched, every step on, filters and sends everywhere; then effects added.
Saves the sequence first and puts it back. seqstress.py"""
import sys,time,struct; sys.path.insert(0,'/Users/you/octa/aeria-os/tools'); import aos, sq, c1dbg
d=sq.d; B=0x100C0000
keep=aos.rd(d,B,0x400+12*64*8)   # the head and pattern A
def op(a,b=0): d.ctrl_transfer(0x40,0x16,a,b,None,timeout=2000); time.sleep(0.05)
def trk(t,i,v): aos.wr(d,B+64+32*t+i,bytes([v]))
def measure(tag,secs=4):
    c1dbg.dspw(d,0x33a06,0); c1dbg.dspw(d,0x33a04,0); c1dbg.dspw(d,0x33a02,0); c1dbg.dspw(d,0x33a05,0); aos.wr(d,0x100FBEE0,bytes(16)); time.sleep(secs)
    r=c1dbg.reports(d,0.5); g=struct.unpack('>4I',aos.rd(d,0x100FBEE0,16))
    print('%-34s dry %4d  overruns %3d  core0 worst %3d%%  core1 late %3d  block %3.0f us avg (%.0f%% of the main processor)  pump gap %.1f ms'%(tag,int(r.get(6,'0'),16),int(r.get(4,'0'),16),int(r.get(2,'0'),16)*100//128,int(r.get(5,'0'),16),g[2]/max(g[1],1)/132,g[2]/max(g[1],1)/132/726*100,g[3]/132000))
try:
    op(0,1); op(4); op(3,1400)
    head=bytearray(aos.rd(d,B,0x400))
    for t in range(8):
        trk(t,0,96+t); trk(t,1,64); trk(t,4,127)
        aos.wr(d,B+0x400+12*(t*64),bytes([1,90,0,0,0,0,0,0,0,0,0,0])*16)
    op(1,1); measure('8 tracks, plain, every step')
    for t in range(8): trk(t,1,71)
    measure('... pitched +7')
    for t in range(8): trk(t,10,30); trk(t,9,50)
    measure('... + drive, delay sends')
    for t in range(8): trk(t,6,30); trk(t,7,100)
    measure('... + HP and LP on all 8')
    for t in range(8): trk(t,12,60); trk(t,14,60)
    measure('... + pitch envelopes, ducking')
    # effects: rows by cost, on core 0 until it overruns
    import re
    src=open('/Users/you/octa/aeria-os/src/fx_table.inc').read()
    cost=[int(x) for x in re.search(r'fx_cost\[FX_COUNT\] = \{([^}]*)\}',src).group(1).split(',')]
    core=[int(x) for x in re.search(r'fx_core\[FX_COUNT\] = \{([^}]*)\}',src).group(1).split(',')]
    names=re.findall(r'\{"AERIA ([A-Z ]+)"',src) or re.findall(r'\{"([A-Za-z ]+)", 0x3',src)
    print('rows',list(zip(range(1,12),cost,core)))
    order=[r for r in range(1,12) if core[r-1]!=1]; tot=0
    for t,r in enumerate(order[:8]):
        aos.wr(d,B+64+256+32+8+t,bytes([0])); aos.wr(d,B+64+256+32+16+12*t,bytes([64,64,64,64,64,64,0,0,0,0,0,0])); aos.wr(d,B+64+256+32+t,bytes([r])); tot+=cost[r-1]
        time.sleep(0.6); measure('  + row %d on track %d (core 0 sum %d)'%(r,t+1,tot),3)
finally:
    op(1,0)
    for t in range(8): aos.wr(d,B+64+256+32+t,bytes([0]))
    time.sleep(0.5); aos.wr(d,B,keep); op(0,keep[5]); print('sequence put back, sequencer',('off','on')[keep[5]])
