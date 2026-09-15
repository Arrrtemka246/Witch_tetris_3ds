#!/usr/bin/env python3
import os, struct, sys
from pathlib import Path
NONE=0xffffffff
def align4(n): return (n+3)&~3
def hash_count(n):
    c=n
    if c<3:c=3
    elif c<19:c|=1
    else:
        while any(c%d==0 for d in (2,3,5,7,11,13,17)): c+=1
    return c
def utf16_units(s):
    raw=s.encode('utf-16le')
    return [struct.unpack_from('<H',raw,i)[0] for i in range(0,len(raw),2)]
def calc_hash(parent,name,total):
    h=(parent ^ 123456789)&0xffffffff
    for ch in utf16_units(name):
        h=((h>>5)|((h<<27)&0xffffffff))&0xffffffff
        h^=ch
    return h%total
class D:
    def __init__(self,name,parent,path):
        self.name=name;self.parent=parent;self.path=path;self.children=[];self.files=[];self.off=0;self.sibling=None;self.nextHash=NONE
class F:
    def __init__(self,name,parent,path):
        self.name=name;self.parent=parent;self.path=path;self.off=0;self.sibling=None;self.nextHash=NONE;self.dataOff=0;self.dataSize=path.stat().st_size
def scan(rootpath):
    root=D('',None,rootpath);root.parent=root;dirs=[root];files=[]
    def rec(d):
        ents=sorted(d.path.iterdir(),key=lambda p:p.name.lower());ds=[];fs=[]
        for p in ents:
            if p.is_dir():
                x=D(p.name,d,p);dirs.append(x);ds.append(x);rec(x)
            elif p.is_file():
                x=F(p.name,d,p);files.append(x);fs.append(x)
        d.children=ds;d.files=fs
        for a,b in zip(ds,ds[1:]):a.sibling=b
        for a,b in zip(fs,fs[1:]):a.sibling=b
    rec(root);return root,dirs,files
def build(rootpath,outpath):
    rootpath=Path(rootpath);root,dirs,files=scan(rootpath)
    off=0
    for d in dirs:
        d.off=off;off+=align4(24+len(d.name.encode('utf-16le')))
    dir_meta_size=off;off=0
    for f in files:
        f.off=off;off+=align4(32+len(f.name.encode('utf-16le')))
    file_meta_size=off;off=0
    for f in files:
        f.dataOff=off;off=align4(off+f.dataSize)
    data_size=off;dhn=hash_count(len(dirs));fhn=hash_count(len(files))
    dht=[NONE]*dhn;fht=[NONE]*fhn
    for d in dirs:
        h=calc_hash(d.parent.off,d.name,dhn);d.nextHash=dht[h];dht[h]=d.off
    for f in files:
        h=calc_hash(f.parent.off,f.name,fhn);f.nextHash=fht[h];fht[h]=f.off
    dh_off=0x28;dh_size=dhn*4;dm_off=dh_off+dh_size
    fh_off=dm_off+dir_meta_size;fh_size=fhn*4;fm_off=fh_off+fh_size;data_off=fm_off+file_meta_size
    out=bytearray(data_off+data_size)
    struct.pack_into('<10I',out,0,0x28,dh_off,dh_size,dm_off,dir_meta_size,fh_off,fh_size,fm_off,file_meta_size,data_off)
    for i,v in enumerate(dht):struct.pack_into('<I',out,dh_off+i*4,v)
    for d in dirs:
        p=dm_off+d.off;name=d.name.encode('utf-16le')
        struct.pack_into('<6I',out,p,d.parent.off,d.sibling.off if d.sibling else NONE,d.children[0].off if d.children else NONE,d.files[0].off if d.files else NONE,d.nextHash,len(name))
        out[p+24:p+24+len(name)]=name
    for i,v in enumerate(fht):struct.pack_into('<I',out,fh_off+i*4,v)
    for f in files:
        p=fm_off+f.off;name=f.name.encode('utf-16le')
        struct.pack_into('<IIQQII',out,p,f.parent.off,f.sibling.off if f.sibling else NONE,f.dataOff,f.dataSize,f.nextHash,len(name))
        out[p+32:p+32+len(name)]=name
        raw=f.path.read_bytes();out[data_off+f.dataOff:data_off+f.dataOff+len(raw)]=raw
    Path(outpath).write_bytes(out)
    print('built',len(dirs),'dirs',len(files),'files','size',len(out),'data',hex(data_off))
if __name__=='__main__': build(sys.argv[1],sys.argv[2])
