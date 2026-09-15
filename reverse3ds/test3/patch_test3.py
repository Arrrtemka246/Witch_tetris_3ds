#!/usr/bin/env python3
from pathlib import Path
import struct,hashlib,sys
src=Path(sys.argv[1]);rom=Path(sys.argv[2]).read_bytes();outp=Path(sys.argv[3])
b=bytearray(src.read_bytes())
sha=hashlib.sha256(b).hexdigest()
assert sha=='73894e036fed69f4af0bb4407874932a5bac6eac6dbca3d546055418f7aaf8dc',sha
magic,hsize,rhsize,ver,flags,cs,rs,ds,bss=struct.unpack_from('<IHHIIIIII',b,0)
assert magic==0x58534433
code_off=hsize+3*rhsize;ro_off=code_off+cs;ro_addr=0x100000+((cs+0xfff)&~0xfff)
smdh_off,smdh_sz,romfs_off=struct.unpack_from('<III',b,32)
def fileoff(va):
    if 0x100000<=va<0x100000+cs:return code_off+(va-0x100000)
    if ro_addr<=va<ro_addr+rs:return ro_off+(va-ro_addr)
    raise ValueError(hex(va))
def word(va): return struct.unpack_from('<I',b,fileoff(va))[0]
def patch_word(va,old,new,note):
    got=word(va);assert got==old,(note,hex(va),hex(got),hex(old))
    struct.pack_into('<I',b,fileoff(va),new);print(note,hex(va),hex(old),'->',hex(new))
def patch_bytes_va(va,old,new,note):
    off=fileoff(va);got=bytes(b[off:off+len(old)]);assert got==old,(note,hex(va),got,old)
    assert len(new)<=len(old);b[off:off+len(old)]=new+b'\0'*(len(old)-len(new));print(note,hex(va))
def enc_b(src,dst,cond=0xE):
    imm=(dst-(src+8))>>2;assert -(1<<23)<=imm<(1<<23)
    return (cond<<28)|0x0A000000|(imm&0xffffff)

# TEST2 cumulative base: keep old working behaviour/resources and hide room from main menu.
patch_word(0x0010831c,0xe3540005,0xe3540004,'menu render 4')
patch_word(0x00113150,0xe3a01005,0xe3a01004,'menu up wrap 4')
patch_word(0x00113170,0xe3a01005,0xe3a01004,'menu down wrap 4')
patch_word(0x00113190,0xe3550003,0xe3550002,'index 3 EXIT')
room_ptr_off=ro_off+0x1a8;exit_ptr_off=ro_off+0x1ac
assert struct.unpack_from('<I',b,room_ptr_off)[0]==0x00071d2c
assert struct.unpack_from('<I',b,exit_ptr_off)[0]==0x00071d38
struct.pack_into('<I',b,room_ptr_off,0x00071d38)

# Browserless ChatGPT winner-choice secret (same mechanism as TEST2).
patch_bytes_va(0x001709c4,b'JETIX\0\0\0',b'CHATGPT\0','CHATGPT alias')
old_ru='ДЖЕТИКС'.encode()+b'\0\0';new_ru='ГПТ'.encode()+b'\0'
patch_bytes_va(0x001709cc,old_ru,new_ru,'ГПТ alias')
CAVE=0x00100dc4
patch_word(0x0010eb5c,0x1a0000d6,enc_b(0x0010eb5c,CAVE,0x1),'ChatGPT context gate')
for i,w in enumerate([
    0xe59dc0d4,0xe31c0a01,enc_b(CAVE+8,0x0010eebc,0x1),enc_b(CAVE+12,0x0010eb60)
]): patch_word(CAVE+i*4,0,w,'ChatGPT cave')
DEPTH=CAVE+16
patch_word(0x00109b60,0xee090a67,enc_b(0x00109b60,DEPTH),'ChatGPT stereo hook')
for i,w in enumerate([0xee090a67,0xee0a0a67,enc_b(DEPTH+8,0x00109b64)]):
    patch_word(DEPTH+i*4,0,w,'stereo cave')
assert word(0x0010ebbc)==0xe5943010 # Do NOT reintroduce TEST1 broad VTD guard.

# TEST3: Phobos Room is inescapable from inside the game.
patch_word(0x00112cd0,0x1a00045a,0xe320f000,'disable B/START room exit')

# Replace the disconnected first room chatter with route-aware short copy that fits original slots.
patch_bytes_va(0x00170818,'Левый Shift?'.encode()+b'\0','Вот и всё.'.encode()+b'\0','room intro line 1')
patch_bytes_va(0x0017082c,'Раньше он мог быть HOLD.'.encode()+b'\0','Ты сам выбрал меня.'.encode()+b'\0','room intro line 2')
patch_bytes_va(0x0017086c,b'B / START: MENU\0',b'CLOSE VIA HOME\0','room exit hint')

# Replace RomFS, preserve original SMDH/header and patched native program.
out=bytes(b[:romfs_off])+rom
outp.write_bytes(out)
print('wrote',outp,'size',len(out),'sha256',hashlib.sha256(out).hexdigest())
