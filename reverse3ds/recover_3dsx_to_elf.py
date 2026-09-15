#!/usr/bin/env python3
import struct, sys
from pathlib import Path

src=Path(sys.argv[1]); out=Path(sys.argv[2])
d=src.read_bytes()
magic,hsize,rhsize,ver,flags,code_sz,ro_sz,data_sz,bss_sz=struct.unpack_from('<IHHIIIIII',d,0)
assert magic==0x58534433
nrt=rhsize//4
relhdr=[]; off=hsize
for _ in range(3):
    relhdr.append(list(struct.unpack_from('<'+'I'*nrt,d,off))); off+=rhsize
code=bytearray(d[off:off+code_sz]); off+=code_sz
rodata=bytearray(d[off:off+ro_sz]); off+=ro_sz
data_load=data_sz-bss_sz
data=bytearray(d[off:off+data_load])+bytearray(bss_sz); off+=data_load
reloc_off=off
base=0x00100000
align=lambda x:(x+0xfff)&~0xfff
sizes=[align(code_sz),align(ro_sz),align(data_sz)]
addrs=[base,base+sizes[0],base+sizes[0]+sizes[1]]
mem=[bytearray(sizes[i]) for i in range(3)]
for i,s in enumerate((code,rodata,data)): mem[i][:len(s)]=s
limits=[sizes[0],sizes[0]+sizes[1]]
def translate(v):
    x=v & 0x0fffffff
    if x<limits[0]: return addrs[0]+x
    if x<limits[1]: return addrs[1]+x-limits[0]
    return addrs[2]+x-limits[1]
rpos=reloc_off; patched=0
for si in range(3):
    for table in range(nrt):
        pos=0
        for _ in range(relhdr[si][table]):
            skip,patch=struct.unpack_from('<HH',d,rpos); rpos+=4; pos+=skip
            for _ in range(patch):
                bo=pos*4
                if bo+4>len(mem[si]): break
                orig=struct.unpack_from('<I',mem[si],bo)[0]
                subtype=orig>>28; addr=translate(orig); inaddr=addrs[si]+bo
                if table==0:
                    if subtype: raise RuntimeError('unsupported abs subtype')
                    val=addr
                elif table==1:
                    delta=(addr-inaddr)&0xffffffff
                    if subtype==0: val=delta
                    elif subtype==1: val=delta & 0x7fffffff
                    else: raise RuntimeError('unsupported rel subtype')
                else:
                    val=orig
                struct.pack_into('<I',mem[si],bo,val); pos+=1; patched+=1

text=bytes(mem[0][:code_sz]); ro=bytes(mem[1][:ro_sz]); dat=bytes(mem[2][:data_load])
EI=b'\x7fELF'+bytes([1,1,1,0])+bytes(8)
EH,PH,SH=52,32,40; phnum=3
fo_text=0x1000
fo_ro=(fo_text+len(text)+0xfff)&~0xfff
fo_data=(fo_ro+len(ro)+0xfff)&~0xfff
shstr=b'\x00.text\x00.rodata\x00.data\x00.bss\x00.shstrtab\x00'
names={n:shstr.index(n.encode()) for n in ['.text','.rodata','.data','.bss','.shstrtab']}
shstr_off=fo_data+len(dat); shoff=(shstr_off+len(shstr)+3)&~3
elf=bytearray(EI)
elf += struct.pack('<HHIIIIIHHHHHH',2,40,1,base,EH,shoff,0x05000000,EH,PH,phnum,SH,6,5)
elf += struct.pack('<IIIIIIII',1,fo_text,addrs[0],addrs[0],len(text),sizes[0],5,0x1000)
elf += struct.pack('<IIIIIIII',1,fo_ro,addrs[1],addrs[1],len(ro),sizes[1],4,0x1000)
elf += struct.pack('<IIIIIIII',1,fo_data,addrs[2],addrs[2],len(dat),sizes[2],6,0x1000)
def pad(n):
    if len(elf)<n: elf.extend(b'\0'*(n-len(elf)))
pad(fo_text); elf+=text; pad(fo_ro); elf+=ro; pad(fo_data); elf+=dat
pad(shstr_off); elf+=shstr; pad(shoff)
elf += bytes(SH)
elf += struct.pack('<IIIIIIIIII',names['.text'],1,6,addrs[0],fo_text,len(text),0,0,4,0)
elf += struct.pack('<IIIIIIIIII',names['.rodata'],1,2,addrs[1],fo_ro,len(ro),0,0,4,0)
elf += struct.pack('<IIIIIIIIII',names['.data'],1,3,addrs[2],fo_data,len(dat),0,0,4,0)
elf += struct.pack('<IIIIIIIIII',names['.bss'],8,3,addrs[2]+data_load,0,bss_sz,0,0,4,0)
elf += struct.pack('<IIIIIIIIII',names['.shstrtab'],3,0,0,shstr_off,len(shstr),0,0,1,0)
out.write_bytes(elf)
print(f'ELF written: {out} patched={patched} text={hex(addrs[0])} ro={hex(addrs[1])} data={hex(addrs[2])}')
