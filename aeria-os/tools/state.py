import sys,struct; sys.path.insert(0,'/Users/femishonugafleming/octa/aeria-os/tools'); import aos
d=aos.dev(); b=aos.rd(d,0x100E0000,1024); o=8+2*11*12+16+96
print(sys.argv[1] if len(sys.argv)>1 else '', b[:4], [struct.unpack('6b',b[o+i*6:o+6+i*6]) for i in range(5)], 'presets saved:', sum(1 for i in range(1,17) if aos.rd(d,0x100E0000+1024*i,4)[:3]==b'AES'))
