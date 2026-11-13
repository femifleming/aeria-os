"""the sequencer's MIDI through the loopback cable (OUT -> IN): what it sends, and how it follows a clock. seqmidi.py"""
import sys,time,struct; sys.path.insert(0,'/Users/you/octa/aeria-os/tools'); import aos, sq
d=sq.d; B=0x100C0000; _Y={l.split()[-1]:int(l.split()[0],16) for l in open('/private/tmp/claude-501/-Users-you-octa/a6e8c4e0-16ca-4199-84ec-2e9cf68c9ec3/scratchpad/sym.txt')}; LB,LN,LT,SQ=_Y['mlog_b'],_Y['mlog_n'],_Y['mlog_t'],_Y['sq']
def op(a,b=0): d.ctrl_transfer(0x40,0x16,a,b,None,timeout=2000)
def tp(t,i,v): aos.wr(d,(B+64+32*t+i) if i<32 else (B+64+256+32+16+96+16*t+(i-32)),bytes([v]))
def log():
    n=struct.unpack('>I',aos.rdc(d,LN,4))[0]; b=aos.rdc(d,LB,512); t=struct.unpack('>512I',aos.rdc(d,LT,2048))
    k=min(n,512); return [(t[(n-k+i)&511],b[(n-k+i)&511]) for i in range(k)]
def clear(): aos.wrc(d,LN,bytes(4))
def parse(ev):
    out=[]; st=None; data=[]
    for t,b in ev:
        if b>=0xF8: out.append((t,[b])); continue
        if b&0x80: st=b; data=[]; need=1 if (b&0xF0) in (0xC0,0xD0) else 2; continue
        if st is None: continue
        data.append(b)
        if len(data)==(1 if (st&0xF0) in (0xC0,0xD0) else 2): out.append((t,[st]+data)); data=[]
    return out
keep=aos.rd(d,B,0x400+12*64*8)
try:
    op(0,1); time.sleep(0.1); op(4); time.sleep(0.1); op(0,1); op(3,1200)
    aos.wr(d,B+6,bytes([1,3]))                      # SYNC send, program changes on channel 3
    T=0; tp(T,19,5); tp(T,20,2); tp(T,21,60); tp(T,41,3); tp(T,42,4); tp(T,43,10)     # MIDI machine, ch 2, C4, MAJ chord, gate 4/8, program 9
    for n in (0,4,8,12): aos.wr(d,B+0x400+12*n,bytes([1,100,0,0,0,0,0,0,0,0,0,0]))
    aos.wr(d,B+0x400+12*8,bytes([1,90,0,0,0,0, 42,22,0, 8,65,0]))    # step 9: CHRD locked to MIN7 (8), NOTE locked to 65
    time.sleep(0.3); clear(); op(1,1); time.sleep(2.05); op(1,0); time.sleep(0.2)
    ev=parse(log()); t0=ev[0][0] if ev else 0
    clk=[t for t,m in ev if m==[0xF8]]; iv=[(clk[i+1]-clk[i])/132000 for i in range(len(clk)-1)]
    print('clocks %d, spacing ms: mean %.3f (20.833 at 120 BPM) min %.2f max %.2f'%(len(clk),sum(iv)/len(iv),min(iv),max(iv)))
    for t,m in ev:
        if m!=[0xF8]: print('  %7.1f ms  %s'%((t-t0)/132000,' '.join('%02X'%x for x in m)))
    # pattern change -> program change on channel 3
    clear(); aos.wr(d,B+9,bytes([1])); op(1,1); time.sleep(0.3); aos.wr(d,B+9,bytes([2])); time.sleep(2.2); op(1,0); time.sleep(0.2)
    print('pattern changes:',[' '.join('%02X'%x for x in m) for t,m in parse(log()) if (m[0]&0xF0)==0xC0])
    # --- following a clock from outside: SYNC recv; ticks injected over the link at 100 BPM (25 ms)
    aos.wr(d,B+6,bytes([2,3])); aos.wr(d,B+9,bytes([0])); time.sleep(0.2)
    op(7,0xFA); t=time.perf_counter(); n=0
    while n<192:
        while time.perf_counter()<t+n*0.025: pass
        op(7,0xF8); n+=1
    s=aos.rdc(d,SQ,8); playing=s[0]; sl=struct.unpack('>I',s[4:8])[0]
    print('after START + 192 ticks at 25 ms: playing %d, step %.2f ms (150.00 at 100 BPM) = %.1f BPM'%(playing,sl/256/44.1,60000/(sl/256/44.1*4)))
    op(7,0xC2); op(7,3); time.sleep(0.1); print('program change 3 on channel 3 -> pattern wanted',aos.rd(d,B+9,1)[0])
    op(7,0xFC); time.sleep(0.1); print('after STOP: playing',aos.rdc(d,SQ,1)[0])
finally:
    op(1,0); time.sleep(0.3); aos.wr(d,B,keep); op(0,keep[5]); print('put back')
