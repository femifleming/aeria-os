"""the style features of 16 Nov 2026 on the unit: equal slices and LEN, SUB waves, the filter envelope, Euclid hits,
the song (its view and its playing). features2_test.py"""
import sys; sys.path.insert(0,'/Users/you/octa/aeria-os/tools'); from ut import *
import numpy as np
STEP=lambda p,t,n: B+0x400+12*((p*8+t)*64+n)
TP=dict(SMPL=0,PTCH=1,STRT=2,ATK=3,DEC=4,VOL=5,HP=6,LP=7,PAN=8,DLY=9,MACH=19,OUT=26)
T3=dict(HUMT=0,HUMV=1,VLP=2,VPT=3,TIN=4,SLCS=5,SLIC=6,LEN=7,FENV=8,FDEC=9,WAVE=10,DTUN=11,SUB2=12,EUC=13)
SONG_N,SONG_ON,SONG_ROW,SONG=765,766,767,769
def rd(a,n): return aos.rd(d,a,n)
def wr(a,b): aos.wr(d,a,b)
def hd(off,n=1): return rd(B+off,n)
def set_trk(t,i,v): wr(B+64+32*t+i,bytes([v&255]))
def set_trk3(t,name,v): wr(B+637+16*t+T3[name],bytes([v&255]))
def trk3(t,name): return rd(B+637+16*t+T3[name],1)[0]
def put_step(p,t,n,flags=1,vel=100,locks=()):
    b=[flags,vel,0,0,0,0,0,0,0,0,0,0]
    for k,(lp,lv) in enumerate(locks): b[6+k]=lp; b[9+k]=lv
    wr(STEP(p,t,n),bytes(b))
def clear_pattern(p=0): wr(B+0x400+12*64*8*p,bytes(12*64*8))
fails=[]
def check(cond,msg):
    print(('ok   ' if cond else 'FAIL ')+msg)
    if not cond: fails.append(msg)
def rms_db(x): return 20*np.log10(max(float(np.sqrt((x**2).mean())),1e-7))
def wins(x,ch,w=5512): return [rms_db(x[i:i+w,ch]) for i in range(0,len(x)-w+1,w)]
def brightness(x,ch):
    """energy above 2 kHz over all, dB"""
    y=x[:,ch]-x[:,ch].mean(); F=np.abs(np.fft.rfft(y*np.hanning(len(y)))); fr=np.fft.rfftfreq(len(y),1/44100.0)
    hi=(F[fr>2000]**2).sum(); al=(F**2).sum()+1e-12; return 10*np.log10(hi/al+1e-12)
def grab(secs=2.1,name='f2'):
    key(K['PLAY']); time.sleep(0.35); x=cap.rec(secs,name); key(K['STOP']); time.sleep(0.3); return x
def page(name,second):
    key(K['NO']); time.sleep(0.1); key(K[name]); time.sleep(0.15)
    first={'SRC':'SMPL','AMP':'ATK','ARR':'DENS','FX1':'HP','LFO':'WAVE','PAGE':'LEN'}[name]
    for _ in range(3):
        enc(0,1); lab=toast().split(' ')[0]; enc(0,-1)
        if (lab!=first)==bool(second): return
        key(K[name]); time.sleep(0.1)
    raise SystemExit('page %s set %d not reached (%s)'%(name,second,toast()))
keep=seq_backup(); old=inputs_off()
try:
    seqmode(1); alive('enter'); key(K['STOP'])
    clear_pattern(0); clear_pattern(1); wr(MT+0x400,bytes(8*64*8))
    wr(B+9,bytes([0])); wr(B+13,bytes([0])); wr(B+22,bytes([16])); wr(B+23,bytes([16])); wr(B+46,bytes([1])); wr(B+10,bytes([0])); wr(B+636,bytes([0]))
    wr(B+SONG_N,bytes([0,0,0]))
    for t in range(8):
        set_trk(t,TP['MACH'],0); set_trk(t,TP['VOL'],100 if t<2 else 0); set_trk(t,TP['OUT'],0); set_trk(t,TP['PTCH'],64); set_trk(t,TP['STRT'],0); set_trk(t,TP['ATK'],0); set_trk(t,TP['DEC'],127); set_trk(t,TP['LP'],127); set_trk(t,TP['HP'],0)
        wr(B+320+t,bytes([16])); wr(B+320+8+t,bytes([16]))
        for nm in T3: set_trk3(t,nm,0)
    set_trk(1,TP['VOL'],0)
    key(K['NO']); key(K['NO']); key(K['TRACK']+0); time.sleep(0.1)
    # ================= the machine knobs on SRC's second set follow the machine
    page('SRC',1); set_trk(0,TP['MACH'],0); time.sleep(0.1); enc(0,2); t1=toast(); enc(0,-2)
    check(t1.startswith('SLCS') and trk3(0,'SLCS')==0,'SMPL machine: knob 7 is SLCS (%s)'%t1)
    enc(1,3); t2=toast(); enc(2,5); t3=toast(); check(t2.startswith('SLIC') and t3.startswith('LEN') and trk3(0,'SLIC')==3 and trk3(0,'LEN')==5,'... SLIC and LEN (%s, %s)'%(t2,t3))
    set_trk3(0,'SLIC',0); set_trk3(0,'LEN',0)
    set_trk(0,TP['MACH'],1); time.sleep(0.1); enc(0,1); t1=toast(); enc(1,10); t2=toast(); enc(2,20); t3=toast()
    check(t1.startswith('WAVE') and 'TRI' in t1 and t2.startswith('DTUN') and t3.startswith('SUB2') and trk3(0,'WAVE')==1 and trk3(0,'DTUN')==10 and trk3(0,'SUB2')==20,'SUB machine: WAVE DTUN SUB2 (%s, %s, %s)'%(t1,t2,t3))
    set_trk(0,TP['MACH'],6); time.sleep(0.1); enc(0,1); t1=toast(); enc(0,-1); check(t1.startswith('TIN'),'THRU machine: TIN (%s)'%t1)
    set_trk(0,TP['MACH'],2); time.sleep(0.1); enc(0,1); t1=toast(); enc(0,-1); check(t1.startswith('RTIO'),'FM machine: RTIO (%s)'%t1)
    set_trk(0,TP['MACH'],0)
    page('LFO',1); enc(0,10); t1=toast(); enc(1,-5); t2=toast(); enc(2,3); t3=toast()
    check(t1.startswith('FENV') and '+10' in t1 and trk3(0,'FENV')==74,'MOD 2: FENV from the middle (%s)'%t1)
    check(t2.startswith('FDEC') and t3.startswith('EUC') and trk3(0,'EUC')==3,'MOD 2: FDEC, EUC (%s, %s)'%(t2,t3))
    set_trk3(0,'FENV',0); set_trk3(0,'FDEC',0); set_trk3(0,'EUC',0); alive('knobs')
    # ================= LEN: the long sample cut short at each step
    set_trk(0,TP['SMPL'],102); set_trk3(0,'SLCS',4)
    for n in (0,4,8,12): put_step(0,0,n,1,110)
    x=grab(2.1,'len_all'); a=wins(x,4)
    set_trk3(0,'LEN',5); y=grab(2.1,'len_short'); b=wins(y,4)
    sus_a=np.median([a[i] for i in range(len(a)) if i%4 in (2,3)]); sus_b=np.median([b[i] for i in range(len(b)) if i%4 in (2,3)])
    print('   between the steps: LEN all %.0f dB, LEN 5 %.0f dB'%(sus_a,sus_b))
    check(sus_a>-60 and sus_b<sus_a-15,'LEN cuts the sample short between steps')
    set_trk3(0,'LEN',0)
    # ================= SLIC: the voice begins at the slice's own frame (read from the voice itself), and a lock a step picks one
    sptr,sframes=smp(102)[0],smp(102)[1]
    for n in (4,8,12): put_step(0,0,n,0,0)
    ok=True
    for k in (1,3,5,8):
        set_trk3(0,'SLIC',k); key(K['PLAY']); time.sleep(0.25); q,st,nn=struct.unpack('>IiI',aos.rdc(d,SYM['tv'],12)); key(K['STOP']); time.sleep(0.25)
        want=sptr+8*(((k-1)%4)*(sframes//4)); okk=(q==want and nn==sframes//4); ok=ok and okk     # a slice number past the count wraps
        print('   SLIC %d: voice begins at +%d frames (slice %d of 4 starts at +%d), %d frames long%s'%(k,(q-sptr)//8,k,(want-sptr)//8,nn,'' if okk else '  <-- wrong'))
    check(ok,'SLIC picks the slice: the voice starts at its frame and plays its length')
    set_trk3(0,'SLIC',0)
    for k,n in enumerate((0,4,8,12)): put_step(0,0,n,1,110,[(T3['SLIC']+48+1,2+2*k)])
    z=grab(2.1,'slices'); c=wins(z,4); on=[c[i] for i in range(0,16,4) if i<len(c)]
    print('   slice steps by locks: %s'%[int(v) for v in on])
    check(len(on)==4 and sum(1 for v in on if v>-60)>=2,'SLIC locks a step: the steps sound their slices')
    for n in (0,4,8,12): put_step(0,0,n,1,110)
    set_trk3(0,'SLCS',0)
    # ================= SUB waves: a saw is brighter than a sine
    set_trk(0,TP['MACH'],1); set_trk(0,TP['PTCH'],52); set_trk(0,TP['DEC'],100); set_trk3(0,'WAVE',0)
    s0=grab(1.6,'sine'); b0=brightness(s0,4)
    set_trk3(0,'WAVE',2); s2=grab(1.6,'saw'); b2=brightness(s2,4)
    set_trk3(0,'WAVE',3); s3=grab(1.6,'sqr'); b3=brightness(s3,4)
    print('   brightness above 2 kHz: sine %.0f dB, saw %.0f dB, square %.0f dB; levels %.0f / %.0f / %.0f'%(b0,b2,b3,max(wins(s0,4)),max(wins(s2,4)),max(wins(s3,4))))
    check(max(wins(s0,4))>-50 and b2>b0+6 and b3>b0+6,'SUB: saw and square carry far more highs than the sine')
    set_trk3(0,'WAVE',0); set_trk3(0,'DTUN',30); set_trk3(0,'SUB2',127); s4=grab(1.6,'detune'); print('   detuned second oscillator: level %.0f dB'%max(wins(s4,4)))
    check(max(wins(s4,4))>-50,'SUB: DTUN / SUB2 sound')
    set_trk3(0,'DTUN',0); set_trk3(0,'SUB2',0); set_trk(0,TP['MACH'],0); set_trk(0,TP['PTCH'],64); set_trk(0,TP['SMPL'],96); set_trk(0,TP['DEC'],127)
    alive('sub')
    # ================= the filter envelope: a closed low-pass opened by FENV at each hit
    set_trk(0,TP['LP'],25); set_trk3(0,'FENV',0); f0=grab(1.6,'fenv_off'); bf0=brightness(f0,4)
    set_trk3(0,'FENV',127); set_trk3(0,'FDEC',60); f1=grab(1.6,'fenv_on'); bf1=brightness(f1,4)
    print('   kick through LP 25: brightness %.0f dB; with FENV +63 FDEC 60: %.0f dB'%(bf0,bf1))
    check(bf1>bf0+4,'FENV opens the low-pass on each hit')
    set_trk(0,TP['LP'],127); set_trk3(0,'FENV',0); set_trk3(0,'FDEC',0)
    # ================= Euclid: five hits over sixteen steps from an empty grid
    clear_pattern(0); set_trk3(0,'EUC',5); set_trk(0,TP['DEC'],40)
    e=grab(2.15,'euclid'); w=wins(e,4)
    hits=[i for i in range(16) if i<len(w) and w[i]>-45]
    print('   loud 125 ms windows: %s (Bjorklund 5/16 gives steps 3 6 9 12 15, the capture starts mid-bar)'%hits)
    check(len(hits)==5,'EUC 5: five hits a bar')
    set_trk3(0,'EUC',0)
    # ================= the song: A (kick on 1 5 9 13) once, then B (hat on every step) once, round and round
    for n in (0,4,8,12): put_step(0,0,n,1,110)
    set_trk(1,TP['SMPL'],97); set_trk(1,TP['VOL'],100); set_trk(1,TP['DEC'],30)
    for n in range(16): put_step(1,1,n,1,100)
    combo(K['FUNC'],K['PAGE']); time.sleep(0.2); t1=toast(); check(t1.startswith('SONG') and u8('sq_view')==3,'FUNC + PAGE opens the song view (%s, view %d)'%(t1,u8('sq_view')))
    key(K['YES']); time.sleep(0.1); enc(1,-3); key(K['YES']); time.sleep(0.1); enc(0,1); enc(3,1); time.sleep(0.1)
    sn,so,srow=hd(SONG_N)[0],hd(SONG_ON)[0],hd(SONG_ROW)[0]; rows=[(hd(SONG+2*k)[0]&7,hd(SONG+2*k)[0]>>3,hd(SONG+2*k+1)[0]) for k in range(sn)]
    check(sn==2 and so==1 and rows==[(0,0,1),(1,0,1)],'two rows made with the keys: A x1, B x1, song on (%d rows, on %d, %s)'%(sn,so,rows))
    key(K['UP']); key(K['NO']); time.sleep(0.1)
    key(K['PLAY']); time.sleep(0.3); pats=[]; rowsn=[]
    for i in range(16): pats.append(hd(9)[0]); rowsn.append(hd(SONG_ROW)[0]); time.sleep(0.28)
    x=cap.rec(2.0,'song_b'); key(K['STOP'])
    print('   pattern wanted over 4.5 s: %s rows %s'%(pats,rowsn))
    check(0 in pats and 1 in pats and len(set(rowsn))==2,'the song walks A then B and back')
    wr(B+SONG_ON,bytes([0]))
    print('DONE features 2: %d fails, %s'%(len(fails),loads(0.5)))
    for f in fails: print('   FAIL',f)
finally:
    key(K['STOP']); time.sleep(0.3); key(K['NO']); key(K['NO']); seq_restore(keep); inputs_back(old); print('state put back')
