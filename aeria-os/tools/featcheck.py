"""tonight's features on the hardware, each measured. featcheck.py (battery RAM kept and put back)"""
import sys,time,struct,subprocess; sys.path.insert(0,'/Users/femishonugafleming/octa/aeria-os/tools'); import aos, sq, c1dbg, place
import numpy as np, wave
d=sq.d; B=0x100C0000; S='/private/tmp/claude-501/-Users-femishonugafleming-octa/a6e8c4e0-16ca-4199-84ec-2e9cf68c9ec3/scratchpad'
Y={l.split()[-1]:int(l.split()[0],16) for l in open(S+'/sym.txt')}; PB=place.SYM['params']
def op(a,b=0): d.ctrl_transfer(0x40,0x16,a,b,None,timeout=2000); time.sleep(0.05)
def tp(t,i,v): aos.wr(d,(B+64+32*t+i) if i<32 else (B+64+256+32+16+96+16*t+(i-32)),bytes([v]))
def head(off,v): aos.wr(d,B+off,bytes([v]))
def par(pg,i,v=None):
    a=PB+(pg*12+i)*16+10
    if v is None: return struct.unpack('>hh',aos.rdc(d,a,4))
    aos.wrc(d,a,struct.pack('>hh',v,v))
def xf(v): aos.wrc(d,Y['xfade'],struct.pack('>i',2*v)); aos.wrc(d,Y['preview_xfade'],struct.pack('>I',v)); time.sleep(0.3)   # the raw fader value (0..254) the firmware copies, so a real fader position does not overwrite the test's
def rec(secs):
    old=[par(6,k) for k in (6,7)]
    for k in (6,7): par(6,k,0)
    subprocess.run(['sox','-q','-t','coreaudio','AERIA audio','-c','6','-b','24',S+'/fc.wav','trim','0',str(secs)])
    for i,k in enumerate((6,7)): aos.wrc(d,PB+(6*12+k)*16+10,struct.pack('>hh',*old[i]))
    w=wave.open(S+'/fc.wav'); b=np.frombuffer(w.readframes(w.getnframes()),dtype=np.uint8).reshape(-1,6,3)
    return ((b[:,:,0].astype(np.int32)|(b[:,:,1].astype(np.int32)<<8)|(b[:,:,2].astype(np.int8).astype(np.int32)<<16))/8388608.0)[2205:]
def onsets(y):
    k=64; e=np.abs(y); env=e[:len(e)//k*k].reshape(-1,k).max(1); th=max(env.max()*0.3,1e-4)
    return [i*k for i in range(2,len(env)) if env[i]>th and env[i-2:i].max()<th*0.6]
def cen(y):
    f=np.abs(np.fft.rfft(y*np.hanning(len(y)))); fr=np.fft.rfftfreq(len(y),1/44100); return (f*fr).sum()/max(f.sum(),1e-9)
res=[]
def ok(name,cond,info): res.append((name,cond)); print('%-34s %s  %s'%(name,'PASS' if cond else 'FAIL',info))
keep=aos.rd(d,B,0x400+0x6000); was=sq.seqmode(1); master_keep=[par(10,k) for k in range(12)]
try:
    op(4); op(3,1200); aos.wr(d,B+0x400,bytes(12*64*8))
    # CLK ACCEL on a track that plays every step
    tp(0,19,1); tp(0,1,76); tp(0,4,10)
    for n in range(16): aos.wr(d,B+0x400+12*n,bytes([1,110,0,0,0,0,0,0,0,0,0,0]))
    OFF=0x40+2   # placeholder, found below
    # the head's curv/camt/cseed sit right after nin
    hb=aos.rd(d,B,0x40); 
    import ctypes
    base_curv=None
    # offsets from seq.h: magic4 ver mode sync pcch swing pattern mutes duck_src duck_rel chain_n chain[8] mlen[4] sel solo mscn[16] mscn_ok[2] nin curv camt cseed
    o_nin=4+2+2+3+2+1+8+4+2+16+2; o_curv=o_nin+1
    op(1,1); time.sleep(0.6)
    y=rec(2.6)[:,4]; on=onsets(y); iv=np.diff(on)/44.1
    plain=np.median(iv) if len(iv) else 0
    head(o_curv,1); head(o_curv+1,127); time.sleep(2.2)
    y=rec(2.6)[:,4]; on=onsets(y); iv=np.diff(on)/44.1
    ok('CLK ACCEL bends the steps', len(iv)>8 and iv.max()-iv.min()>plain*0.8, 'plain step %.0f ms; with ACCEL %s ms'%(plain,[int(v) for v in iv[:16]]))
    head(o_curv,0); head(o_curv+1,0)
    # CA GATE: no grid steps, rule 30, track plays the cells; loops differ
    aos.wr(d,B+0x400,bytes(12*64)); tp(0,45,15); tp(0,46,0); tp(0,47,0)
    time.sleep(2.2); y=rec(4.2)[:,4]; on=np.array(onsets(y))
    loop=16*125.4*44.1; rows=[sorted(set(int(round((o%loop)/(loop/16)))%16 for o in on if a*loop<=o<(a+1)*loop)) for a in range(int(len(y)/loop))]
    ok('CA GATE plays the automaton', len(on)>4 and len(set(map(tuple,rows)))>1, 'steps per loop: %s'%rows)
    lv=struct.unpack('>128i',aos.rdc(d,Y['preview_leds'],512))[0:32:2]
    ok('CA cells on the trig lamps', len(set(lv))>1, 'lamp levels %s'%list(lv))
    tp(0,45,0)
    # step lamps: on bright (3), off dim (1)
    for n in (0,4,8,12): aos.wr(d,B+0x400+12*n,bytes([1,110,0,0,0,0,0,0,0,0,0,0]))
    op(1,0); time.sleep(0.4); lv=struct.unpack('>128i',aos.rdc(d,Y['preview_leds'],512))[0:32:2]
    ok('step lamps: on bright, off dim', lv[0]==3 and lv[4]==3 and lv[1]==1 and lv[2]==1, 'lamps 1-8 %s'%list(lv[:8]))
    op(1,1)
    # sequencer scene: track 1 PTCH in scene A = 40, scene B = 100; the crossfader blends
    sa=struct.unpack('>B',aos.rdc(d,Y['scene_a'],1))[0] if True else 0
    sa=aos.rdc(d,Y['scene_a'],1)[0]; sb=aos.rdc(d,Y['scene_b'],1)[0]
    SQSC=0x100ED010; tpi=1
    aos.wr(d,SQSC+(sa*8+0)*48+tpi,bytes([40])); aos.wr(d,SQSC+(sb*8+0)*48+tpi,bytes([100]))
    par(10,9,0)
    xf(0); time.sleep(0.4); a0=struct.unpack('>h',aos.rdc(d,Y['aos_mod_tp']+2*(0*48+tpi),2))[0]
    xf(127); time.sleep(0.4); a1=struct.unpack('>h',aos.rdc(d,Y['aos_mod_tp']+2*(0*48+tpi),2))[0]
    base=aos.rd(d,B+64+tpi,1)[0]
    ok('scenes move a track knob', base+a0==40 and base+a1==100, 'PTCH %d: at A %d, at B %d (40 / 100 wanted)'%(base,base+a0,base+a1))
    aos.wr(d,SQSC+(sa*8+0)*48+tpi,bytes([255])); aos.wr(d,SQSC+(sb*8+0)*48+tpi,bytes([255]))
    # XMOD DJF on noise: left darker, right brighter
    tp(0,19,3); tp(0,4,127)
    for n in range(16): aos.wr(d,B+0x400+12*n,bytes([1,110,0,0,0,0,0,0,0,0,0,0]))
    par(10,9,1); par(6,5,1)
    c={}
    for x in (64,0,127): xf(x); time.sleep(0.8); c[x]=cen(rec(1.5)[:,4])
    ok('crossfader DJ filter', c[0]<c[64]*0.6 and c[127]>c[64]*1.2, 'brightness: middle %d Hz, A %d Hz, B %d Hz'%(c[64],c[0],c[127]))
    # XMOD MOD: an LFO route scaled by the fader
    MODS=0x100EC000; ro=aos.rd(d,MODS+8,4)
    aos.wr(d,MODS+8,bytes([1,40])+struct.pack('>H',3<<12|(0*48+1)))
    par(10,9,3); xf(0); time.sleep(0.5); m0=max(abs(struct.unpack('>h',aos.rdc(d,Y['aos_mod_tp']+2,2))[0]) for _ in range(5) if not time.sleep(0.1))
    xf(127); time.sleep(0.5); m1=max(abs(struct.unpack('>h',aos.rdc(d,Y['aos_mod_tp']+2,2))[0]) for _ in range(8) if not time.sleep(0.1))
    ok('crossfader MOD depth', m0==0 and m1>3, 'offset at A %d, at B up to %d'%(m0,m1))
    aos.wr(d,MODS+8,ro)
    par(10,9,0); xf(64)
finally:
    op(1,0); time.sleep(0.3)
    for k in range(12): aos.wrc(d,PB+(10*12+k)*16+10,struct.pack('>hh',*master_keep[k]))
    aos.wr(d,B,keep); sq.seqmode(was)
# the sky: STOP and the LOOP page
was2=sq.seqmode(0)
try:
    sl=aos.rdc(d,Y['slots'],0x268); wx0=aos.rdc(d,Y['wx_on'],1)[0]
    users=[struct.unpack('>6h',sl[88*s+66:88*s+78]) for s in range(7)]
    sq.key(0x27); time.sleep(0.4)
    sl2=aos.rdc(d,Y['slots'],0x268); wx1=aos.rdc(d,Y['wx_on'],1)[0]
    mixes=[]
    for s in range(7):
        q=sl2[88*s:88*s+88]
        if struct.unpack('>h',q[2:4])[0]>0:
            kn=[q[18+5*k:23+5*k].split(b'\0')[0] for k in range(6)]
            for k in range(6):
                if kn[k]==b'MIX': mixes.append(struct.unpack('>h',q[66+2*k:68+2*k])[0])
    ok('STOP: every effect dry, sky clear', all(m==0 for m in mixes) and wx1 in (0,1), 'MIX holds %s, weathers %02x (was %02x)'%(mixes,wx1,wx0))
    # put the holds and weather back
    for s in range(7): aos.wrc(d,Y['slots']+88*s+66,struct.pack('>6h',*users[s]))
    aos.wrc(d,Y['wx_on'],bytes([wx0]))
    sq.key(0x24); time.sleep(0.3)        # LFO key in the sky? the LOOP page key
    l0=aos.rdc(d,Y['src_loop'],4)
finally:
    sq.seqmode(was2)
print('%d of %d passed'%(sum(1 for _,c in res if c),len(res)))
