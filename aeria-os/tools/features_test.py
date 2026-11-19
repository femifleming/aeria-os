"""the eight features of 15/16 Nov 2026 on the unit: slicer, resample / single WAV, humanise, velocity amounts and
effect-knob locks, eight patterns, bigger banks, THRU, track scenes in projects. features_test.py [quick]"""
import sys; sys.path.insert(0,'/Users/you/octa/aeria-os/tools'); from ut import *
import numpy as np
SQSC=0x100ED010
STEP=lambda p,t,n: B+0x400+12*((p*8+t)*64+n)
MSTEP=lambda p,t,n: MT+0x400+8*((p*8+t)*64+n)
def rd(a,n): return aos.rd(d,a,n)
def wr(a,b): aos.wr(d,a,b)
def hd(off,n=1): return rd(B+off,n)
def trk(t,i): return rd(B+64+32*t+i,1)[0]
def trk3(t,i): return rd(B+637+16*t+i,1)[0]
def set_trk(t,i,v): wr(B+64+32*t+i,bytes([v&255]))
def set_trk3(t,i,v): wr(B+637+16*t+i,bytes([v&255]))
def step(p,t,n): return rd(STEP(p,t,n),12)
def put_step(p,t,n,flags=1,vel=100,locks=()):
    b=[flags,vel,0,0,0,0,0,0,0,0,0,0]
    for k,(lp,lv) in enumerate(locks): b[6+k]=lp; b[9+k]=lv
    wr(STEP(p,t,n),bytes(b))
def clear_pattern(p=0):
    wr(B+0x400+12*64*8*p,bytes(12*64*8))
def clear_mt_pattern(p=0):
    wr(MT+0x400+8*64*8*p,bytes(8*64*8))
fails=[]
def check(cond,msg):
    print(('ok   ' if cond else 'FAIL ')+msg)
    if not cond: fails.append(msg)
_dry=[0]
def stage(tag):
    alive(tag); n=dry(); print('   [%s] dry +%d'%(tag,n-_dry[0])); _dry[0]=n
def wait_toast(words,secs=8.0):
    t=time.time()
    while time.time()-t<secs:
        s=toast()
        if any(w in s for w in words): return s
        time.sleep(0.1)
    return toast()
def page(name,second):
    """a knob page's first or second set in hand (the same key again toggles the set); told apart by knob 0's label"""
    key(K['NO']); time.sleep(0.1); key(K[name]); time.sleep(0.15); print('   page %s: %s'%(name,toast()))
    for _ in range(3):
        enc(0,1); lab=toast().split(' ')[0]; enc(0,-1)
        first={'SRC':'SMPL','AMP':'ATK','ARR':'DENS','FX1':'HP','LFO':'WAVE','PAGE':'LEN'}[name]
        if (lab!=first)==bool(second): return
        key(K[name]); time.sleep(0.1)
    raise SystemExit('page %s set %d not reached (label %s, toast %s)'%(name,second,lab,toast()))
def rms_db(x): return 20*np.log10(max(float(np.sqrt((x**2).mean())),1e-7))
def window_rms(x,ch,w=5512):
    return [rms_db(x[i:i+w,ch]) for i in range(0,len(x)-w+1,w)]
quick=len(sys.argv)>1 and sys.argv[1]=='quick'
keep=seq_backup(); old=inputs_off(); dry0=dry(); _dry[0]=dry0
proj_slot=None; proj_backup=None
try:
    seqmode(1); alive('enter'); key(K['STOP'])
    # ---- a known pattern A: track 1 a kit kick on 1 5 9 13, the rest silent; no notes in from the DIN loop
    clear_pattern(0); clear_mt_pattern(0)
    wr(B+9,bytes([0])); wr(B+13,bytes([0])); wr(B+22,bytes([16])); wr(B+46,bytes([1])); wr(B+10,bytes([0])); wr(B+636,bytes([0]))
    for t in range(8): set_trk(t,19,0); set_trk(t,5,100 if t==0 else 0); set_trk(t,26,0); wr(B+320+t,bytes([16])); set_trk3(t,0,0); set_trk3(t,1,0); set_trk3(t,2,0); set_trk3(t,3,0); set_trk3(t,4,0)
    set_trk(0,0,96); set_trk(0,1,64); set_trk(0,2,0); set_trk(0,3,0); set_trk(0,4,60); set_trk(0,8,64); set_trk(0,9,0); set_trk(0,20,0)   # SMPL PTCH STRT ATK DEC PAN DLY SLIP
    print('   track 1 bytes:',list(rd(B+64,32)))
    for n in (0,4,8,12): put_step(0,0,n,1,110)
    key(K['TRACK']+0); time.sleep(0.1)
    # ================= 5. eight patterns, a chain of sixteen
    key(K['PATTERN'],1); key(K['TRIG']+4); key(K['PATTERN'],0); time.sleep(0.1)
    check(hd(9)[0]==4 and 'PATTERN' in toast() and toast().endswith('E'),'PATTERN + TRIG 5 = pattern E (head %d, toast %s)'%(hd(9)[0],toast()))
    key(K['PATTERN'],1)
    for k in list(range(8))+list(range(8)): key(K['TRIG']+k)
    key(K['PATTERN'],0); time.sleep(0.1)
    check(hd(13)[0]==16 and list(hd(628,8))==list(range(8)) and 'CHAIN' in toast(),'a chain of 16 patterns (chain_n %d, entries 9-16 %s)'%(hd(13)[0],list(hd(628,8))))
    key(K['PATTERN'],1); key(K['TRIG']+0); key(K['PATTERN'],0); time.sleep(0.1)
    check(hd(9)[0]==0 and hd(13)[0]==0,'back to pattern A, chain cleared')
    key(K['PLAY']); time.sleep(0.4); key(K['PATTERN'],1); key(K['TRIG']+7); key(K['PATTERN'],0); time.sleep(2.6)
    check(aos.rdc(d,SYM['sq'],1) is not None and hd(9)[0]==7,'pattern H wanted while playing (head %d)'%hd(9)[0]); key(K['STOP'])
    key(K['PATTERN'],1); key(K['TRIG']+0); key(K['PATTERN'],0); stage('patterns')
    # ================= 4a. the new knobs reach their bytes
    page('SRC',1); enc(3,10); t1=toast(); enc(4,5); t2=toast(); enc(5,1); t3=toast(); rs=hd(636)[0]; enc(5,-1)
    check(trk3(0,2)==10 and t1.startswith('VLP'),'SRC 2: VLP knob -> trk3 (%d, %s)'%(trk3(0,2),t1))
    check(trk3(0,3)==5 and t2.startswith('VPT'),'SRC 2: VPT knob -> trk3 (%d, %s)'%(trk3(0,3),t2))
    check(rs==1 and t3.startswith('RSRC') and 'IN AB' in t3,'SRC 2: RSRC knob -> head (%d, %s)'%(rs,t3))
    page('ARR',1); enc(4,20); t1=toast(); enc(5,30); t2=toast()
    check(trk3(0,0)==20 and t1.startswith('HUMT'),'GEN 2: HUMT knob (%d, %s)'%(trk3(0,0),t1))
    check(trk3(0,1)==30 and t2.startswith('HUMV'),'GEN 2: HUMV knob (%d, %s)'%(trk3(0,1),t2))
    page('SRC',0)
    for _ in range(6): enc(4,1)
    t1=toast()
    for _ in range(6): enc(4,-1)
    check(t1.startswith('MACH') and 'THRU' in t1,'MACH reaches THRU (%s)'%t1)
    set_trk3(0,0,0); set_trk3(0,1,0); set_trk3(0,2,0); set_trk3(0,3,0); stage('knobs')
    # ================= 4b. a lock on an effect knob
    wr(B+352,bytes([1])); time.sleep(0.5)                       # PITCH on track 1 (row 1), by the head: the shelf's cursor would pick whatever comes next
    row=hd(352)[0]; check(row==1,'an effect on track 1 (row %d)'%row)
    put_step(0,0,0,1,110)
    key(K['TRIG']+0,1); time.sleep(0.1); key(K['FX2']); time.sleep(0.1); lk=toast(); enc(0,5); time.sleep(0.1); key(K['TRIG']+0,0)
    s0=step(0,0,0); locks=[(s0[6+k],s0[9+k]) for k in range(3)]
    check(lk.startswith('LOCK') and any(lp==65 for lp,lv in locks),'FX knob 1 locked on step 1 (%s, locks %s)'%(lk,locks))
    key(K['TRIG']+0,1); time.sleep(0.1); key(K['FX2']); time.sleep(0.1); enc(0,-5); time.sleep(0.1); key(K['TRIG']+0,0)
    s0=step(0,0,0); check([(s0[6+k],s0[9+k]) for k in range(3)]!=locks,'the lock moved with the knob')
    put_step(0,0,0,1,110); wr(B+352,bytes([0])); time.sleep(0.3); stage('fx lock')   # the effect off again (its knobs were never given defaults by the shelf)
    # ================= 7. THRU: track 2 lets inputs C D (the phones, carrying the cue) in at its steps
    set_trk(0,0,96); set_trk(0,26,1); set_trk(0,4,127)       # track 1: the kick on every step, to the cue pair only, so main is silent of it (the phones carry it into C D)
    for n in range(16): put_step(0,0,n,1,110)
    set_trk(1,19,6); set_trk3(1,4,1); set_trk(1,3,0); set_trk(1,4,90); set_trk(1,5,127); set_trk(1,7,127); set_trk(1,6,0); set_trk(1,26,0)
    for n in (2,6,10,14): put_step(0,1,n,1,110)
    key(K['PLAY']); time.sleep(0.6); x=cap.rec(3.0,'thru_on'); key(K['STOP']); time.sleep(0.3)
    wm=window_rms(x,4); wc=window_rms(x,2)
    wr(B+10,bytes([2])); time.sleep(0.2)
    key(K['PLAY']); time.sleep(0.6); y=cap.rec(3.0,'thru_off'); key(K['STOP']); wr(B+10,bytes([0]))
    vm=window_rms(y,4)
    print('   main windows with THRU: max %.0f dB, median %.0f; muted: max %.0f; cue in C: max %.0f'%(max(wm),np.median(wm),max(vm),max(wc)))
    check(max(wc)>-40,'the cue-only track is heard in input C (the phones)')
    check(max(wm)>-55 and max(wm)>max(vm)+25,'THRU opens the inputs into the main out at its steps')
    check(np.median(wm)<max(wm)-12,'... and only at its steps')
    set_trk(1,19,0); set_trk3(1,4,0); set_trk(1,5,0)
    for n in (2,6,10,14): put_step(0,1,n,0,0)
    set_trk(0,0,96); set_trk(0,26,0); set_trk(0,4,60)
    for n in range(16): put_step(0,0,n,1 if n % 4 == 0 else 0,110)
    stage('thru')
    # ================= 1. found sound: balloon 1 (inputs A B = the main out on this rig) sliced onto the track
    key(K['TRACK']+0); key(K['PLAY']); time.sleep(0.3)
    key(0x2b); c=toast(); time.sleep(2.3); key(0x2b); f=toast(); time.sleep(0.2)
    check(c.startswith('CATCHING') and f.startswith('BALLOON'),'balloon 1 caught and floated (%s / %s)'%(c,f))
    key(K['STOP']); combo(K['FUNC'],0x2b); time.sleep(1.5); sl=toast()
    n=int(sl.split()[-1]) if sl.startswith('SLICES') else 0
    ln=hd(320)[0]
    ok=n>=2 and trk(0,0)==80 and ln==min(n,16) and all(step(0,0,k)[0]&1 and step(0,0,k)[6]==1 and step(0,0,k)[9]==80+k for k in range(min(n,16))) and all(smp(80+k)[3]==0 and smp(80+k)[1]>0 for k in range(min(n,16)))
    check(ok,'slices: %s; track on 6.1, length %d, steps locked to 6.1.., slots ready (frames %s)'%(sl,ln,[smp(80+k)[1] for k in range(min(n,16))]))
    key(K['PLAY']); time.sleep(0.3); z=cap.rec(2.0,'slices'); key(K['STOP'])
    check(max(window_rms(z,4))>-50,'the slices sound (main max %.0f dB; the kit kick gives about -31, a take is normalised to -3 dBFS peaks)'%max(window_rms(z,4)))
    stage('slicer')
    # ================= 2. resample into the next slot of bank 6, then a single WAV from the browser
    clear_pattern(0); set_trk(0,0,96); wr(B+320,bytes([16]))
    for k in (0,4,8,12): put_step(0,0,k,1,110)
    nxt=80+n
    key(K['PLAY']); time.sleep(0.2); combo(K['FUNC'],K['BANK']); r=toast()
    check(r.startswith('RESAMPLE') and ('6.%d'%(nxt-79)) in r,'resample armed into 6.%d (%s)'%(nxt-79,r))
    got=wait_toast(['TRACK HAS'],9.0); key(K['STOP']); time.sleep(0.3)
    check(got.startswith('TRACK HAS') and trk(0,0)==nxt and smp(nxt)[3]==0 and smp(nxt)[1]>44100,'the take landed on the track (%s, slot %d, %d frames)'%(got,trk(0,0),smp(nxt)[1]))
    key(K['PLAY']); time.sleep(0.3); z=cap.rec(2.0,'resample'); key(K['STOP'])
    check(max(window_rms(z,4))>-50,'the resample sounds (main max %.0f dB; the kit kick gave about -31)'%max(window_rms(z,4)))
    key(K['SRC']); combo(K['FUNC'],K['YES']); time.sleep(0.6)
    for _ in range(3): key(K['YES']); time.sleep(1.0)        # the card: / -> its one folder -> AUDIO -> DRUMS (files first)
    print('   browser at depth: %d entries'%u32('br_count'))
    key(K['YES']); time.sleep(1.5); got=toast()
    check(got.startswith('TRACK HAS') and trk(0,0)==nxt+1 and smp(nxt+1)[3]==0 and smp(nxt+1)[1]>0 and u32('br_mode')==0,'one WAV from the browser onto the track (%s, slot %d, %d frames, browser closed %d)'%(got,trk(0,0),smp(nxt+1)[1],u32('br_mode')==0))
    print('   bank 6 used: %d bytes'%u32('bank_used',20))
    stage('resample / single')
    # ================= 6. the bigger kit bank (the owner's kit came back whole at this start)
    print('   bank 7 used: %d bytes; frames of its slots: %s'%(u32('bank_used',24),[smp(96+k)[1] for k in range(16)]))
    check(u32('bank_used',24)>0x400000 or sum(smp(96+k)[1] for k in range(16))*8<0x400000,'bank 7 holds more than the old 4 MB when the kit needs it')
    # ================= 3. humanise: MIDI track 1 over the DIN loop, timing and velocity
    clear_pattern(0); set_trk(0,0,96)
    LB,LN,LT=SYM['mlog_b'],SYM['mlog_n'],SYM['mlog_t']
    def log():
        n=struct.unpack('>I',aos.rdc(d,LN,4))[0]; b=aos.rdc(d,LB,512); t=struct.unpack('>512I',aos.rdc(d,LT,2048))
        k=min(n,512); return [(t[(n-k+i)&511],b[(n-k+i)&511]) for i in range(k)]
    def notes(ev):
        out=[]; st=None; data=[]
        for t,b in ev:
            if b>=0xF8: continue
            if b&0x80: st=b; data=[]; continue
            if st is None: continue
            data.append(b)
            if len(data)==(1 if (st&0xF0) in (0xC0,0xD0) else 2):
                if (st&0xF0)==0x90 and data[1]>0: out.append((t,data[0],data[1]))
                data=[]
        return out
    for n in range(16): wr(MSTEP(0,0,n),bytes([1,100,0,0,0,0,0,0]))
    wr(MT+40+12,bytes([0,0])); wr(MT+8,bytes([16]))
    def run(secs=2.3):
        aos.wrc(d,LN,bytes(4)); key(K['PLAY']); time.sleep(secs); key(K['STOP']); time.sleep(0.2); return notes(log())
    ev=run(); iv=[((ev[i+1][0]-ev[i][0])&0xFFFFFFFF)/132000.0 for i in range(len(ev)-1)]
    sd0=float(np.std(iv)) if iv else -1; v0=sorted(set(v for t,no,v in ev))
    wr(MT+40+12,bytes([127,127])); ev=run(); iv=[((ev[i+1][0]-ev[i][0])&0xFFFFFFFF)/132000.0 for i in range(len(ev)-1)]
    sd1=float(np.std(iv)) if iv else -1; v1=sorted(set(v for t,no,v in ev))
    print('   M1 notes: plain interval sd %.2f ms, velocities %s; humanised %d notes, sd %.2f ms, velocities %s'%(sd0,v0,len(ev),sd1,v1[:6]))
    check(sd0>=0 and sd0<2.5 and v0==[100],'plain MIDI track: steady timing, one velocity')
    check(sd1>8.0 and len(v1)>3 and max(v1)<=100,'HUMT / HUMV: timing wanders (sd %.1f ms, expect about 15), velocities vary below 100'%sd1)
    wr(MT+40+12,bytes([0,0])); clear_mt_pattern(0); stage('humanise')
    # ================= 8. the track scenes go with a project
    free=[i for i in range(8) if rd(0x10000000+0x17C00*i,4)!=b'AEPJ']
    if not free: print('   no empty project slot: the scenes-in-projects test is skipped')
    else:
        proj_slot=free[0]; pa=0x10000000+0x17C00*proj_slot
        sa=u8('scene_a')
        page('AMP',0); key(K['SCENEA'],1); time.sleep(0.1); enc(2,-20); time.sleep(0.1); key(K['SCENEA'],0)
        v=rd(SQSC+((sa*8+0)*64+5),1)[0]
        check(v!=255 and v!=0,'SCENE A holds track 1 VOL (%d, VOL now %d)'%(v,trk(0,5)))
        def projects_page():
            for _ in range(3):
                if u32('preset_mode')==1: return
                key(K['PROJ']); time.sleep(0.3)
            raise SystemExit('the PROJECTS page would not open')
        projects_page(); key(K['TRIG']+proj_slot); combo(K['FUNC'],K['YES']); time.sleep(0.2); key(K['YES']); got=wait_toast(['SAVED'],12.0)
        if u32('preset_mode')==1: key(K['NO']); time.sleep(0.2)     # the page stays open after a save
        check(got.startswith('SAVED') and rd(pa,4)==b'AEPJ' and rd(pa+0x22,1)[0]==1,'project %d saved with its track scenes (%s)'%(proj_slot+1,got))
        key(K['SCENEA'],1); time.sleep(0.1); enc(2,15); time.sleep(0.1); key(K['SCENEA'],0); v2=rd(SQSC+((sa*8+0)*64+5),1)[0]
        projects_page(); key(K['TRIG']+proj_slot); key(K['YES']); got=wait_toast(['LOADED'],15.0); time.sleep(0.5)
        if u32('preset_mode')==1: key(K['NO'])
        v3=rd(SQSC+((sa*8+0)*64+5),1)[0]
        check(v2!=v and v3==v,'loading it brings the scene back (%d -> %d -> %d, %s)'%(v,v2,v3,got))
        key(K['SCENEA'],1); combo(K['FUNC'],K['TRIG']+0); key(K['SCENEA'],0)   # scene value cleared? (FUNC+trig with SCENE held: the mute scene...) harmless
    print('DONE features: %d fails, dry +%d, %s'%(len(fails),dry()-dry0,loads(0.5)))
    for f in fails: print('   FAIL',f)
finally:
    key(K['STOP']); time.sleep(0.3); seq_restore(keep); inputs_back(old)
    if proj_slot is not None: wr(0x10000000+0x17C00*proj_slot,bytes(4))
    print('state put back (bank 6 keeps the takes; balloon 1 is floating)')
