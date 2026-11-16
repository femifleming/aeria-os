"""walk the sky face's keys on the unit: every page, the shelf, weather, scenes, OUT, cloud, MIDI modules, MASTER, MOD, projects, system. sweep_sky.py"""
import sys; sys.path.insert(0,'/Users/you/octa/aeria-os/tools'); from ut import *
import numpy as np
def knobs():
    for e in range(6): enc(e,2); enc(e,-2)
old=inputs_off(); dry0=dry()
keep=seq_backup(); keepproj=aos.rd(d,0x10040000+15*0x8000,0x8000); keeppm=aos.rd(d,0x10000000+15*0x4000,0x4000)
try:
    seqmode(0); alive('sky')
    for nm in ('SRC','AMP','LFO','FX1','FX1','PART','AED','ARR','MIX','FX2'):
        key(K[nm]); knobs(); key(K[nm]); knobs(); key(K['NO']); alive('page '+nm)
    key(K['AMP']); key(K['TRIG']+2); key(K['TRIG']+6); key(K['TRIG']+11); print('rain+fog+aurora: wx_on',u8('wx_on'),'xw_on',u8('xw_on'),'toast',toast())
    key(K['TRIG']+7); key(K['TRIG']+7); key(K['TRIG']+0); alive('weather'); print('CLEAR: wx_on',u8('wx_on'),'xw_on',u8('xw_on'))
    key(K['UP']); key(K['UP']); key(K['DOWN']); key(K['DOWN']); key(K['LEVELPUSH']); key(K['LEVELPUSH']); key(K['YES']); key(K['YES']); combo(K['FUNC'],K['NO']); alive('weather keys')
    key(K['SRC']); key(K['TRIG']+1); key(K['TRIG']+3); key(K['LEFT']); key(K['RIGHT']); combo(K['FUNC'],K['TRIG']+0); combo(K['FUNC'],K['TRIG']+0)
    key(K['BANK']); key(K['DOWN']); key(K['DOWN']); key(K['NO']); key(K['TRIG']+5); key(K['YES']); print('YES on a slot: shelf_open',i32('shelf_open')); key(K['DOWN']); key(K['DOWN']); key(K['DOWN']); key(K['YES']); time.sleep(1.5); alive('shelf put'); print('slot rows',aos.rdc(d,SYM['slot_row'],8).hex(),'toast',toast(),loads(0.4))
    key(K['SRC']); knobs(); key(K['NO']); key(K['PUSH']+0); alive('chain func')
    key(K['LFO']); key(K['TRIG']+1); key(K['TRIG']+1); combo(K['FUNC'],K['TRIG']+0); time.sleep(0.4); key(K['DOWN']); key(K['RIGHT']); time.sleep(0.5); key(K['LEFT']); key(K['NO']); key(K['NO']); alive('browser')
    key(K['AED']); key(K['PLAY']); time.sleep(1.0); x=cap.rec(1.0,'lp'); print('LOOP playing: main %.0f dB phones %.0f dB'%(cap.db(np.sqrt((x[2205:,0]**2).mean())),cap.db(np.sqrt((x[2205:,2]**2).mean())))); key(K['STOP']); key(K['TRIG']+2); combo(K['FUNC'],K['TRIG']+10); alive('loop')
    key(K['ARR']); key(K['TRIG']+0); key(K['TRIG']+0); key(K['PATTERN']); key(K['PATTERN']); combo(K['FUNC'],K['NO']); alive('stars')
    key(K['AED']); key(K['PLAY']); time.sleep(0.4); key(K['REC3']); time.sleep(1.2); key(K['REC3']); key(K['STOP']); time.sleep(1.0); print('balloon 3 floats: src_balloons',i32('src_balloons'),'voices',len(voices())); combo(K['FUNC'],K['REC3']); alive('balloon')
    key(K['FX1']); key(K['TRACK']+0,1); key(K['TRACK']+1); key(K['TRACK']+0,0); key(K['TRACK']+0,1); key(K['TRACK']+1); key(K['TRACK']+0,0); alive('mix')
    key(K['MIX']); knobs(); key(K['MIX']); knobs(); key(K['NO']); key(K['TRIG']+7); key(K['NO']); key(K['TEMPO']); key(K['TEMPO'],1); enc(6,2); enc(6,-2); key(K['TEMPO'],0); alive('master')
    combo(K['FUNC'],K['SCENEB']); print('FUNC+SCENE B:',toast()); combo(K['FUNC'],K['SCENEA']); print('FUNC+SCENE A:',toast())
    key(K['CUE']); key(K['TRIG']+0); key(K['YES']); knobs(); key(K['PAGE']); knobs(); key(K['PAGE']); key(K['YES']); key(K['NO']); alive('out')
    key(K['REC']); knobs(); key(K['NO']); alive('cloud')
    key(K['MIDI']); key(K['RIGHT']); key(K['RIGHT']); key(K['LEFT']); key(K['LEFT']); print('midi module',i32('midi_mod'))
    for m in range(4):
        key(K['MIDI']); time.sleep(0.1); key(K['YES']); knobs(); key(K['PAGE']); knobs(); key(K['PAGE']); key(K['YES']); alive('module %d'%i32('midi_mod'))
    key(K['AMP']); key(K['SCENEA'],1); key(K['TRIG']+2); enc(0,3); key(K['SCENEA'],0); key(K['SCENEB'],1); key(K['TRIG']+3); key(K['SCENEB'],0); alive('scenes')
    combo(K['FUNC'],K['ARR']); key(K['TRIG']+15); knobs(); key(K['YES']); key(K['AMP']); enc(0,1); combo(K['FUNC'],K['ARR']); key(K['TRIG']+15); combo(K['FUNC'],K['YES']); key(K['NO']); alive('mod')
    key(K['PROJ']); key(K['TRIG']+15); combo(K['FUNC'],K['YES']); 
    for k in (9,9,2,8,13,13): key(K['TRIG']+k)
    key(K['YES']); time.sleep(2.0); alive('save'); print('saved:',toast()); key(K['TRIG']+15); key(K['YES']); time.sleep(6.0); alive('load'); print('loaded:',toast())
    combo(K['FUNC'],K['PROJ']); key(K['NO']); alive('init ask')
    key(K['FX2']); enc(4,1); key(K['YES']); time.sleep(1.5); print('saver on:',i32('saver_on')); key(K['NO']); enc(4,-1); key(K['FX2']); enc(0,1); print('lang:',toast()); enc(0,-1); key(K['NO']); combo(K['FUNC'],K['YES']); alive('system')
    key(K['LEFT']); key(K['RIGHT']); key(K['NO']); key(K['NO']); alive('misc')
    print('DONE sky sweep: dry +%d, %s'%(dry()-dry0,loads(0.5)))
finally:
    seq_restore(keep); aos.wr(d,0x10040000+15*0x8000,keepproj); aos.wr(d,0x10000000+15*0x4000,keeppm); inputs_back(old); print('state regions put back (the sky state autosaves: restore the battery backup before the final flash)')
