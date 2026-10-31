#!/usr/bin/env python3
"""Original PSP homebrew: real GE list + HLE imports, no game/firmware data."""
import struct
from pathlib import Path

def make_elf(sprite=False,large_sprite=False,wrap=False,zero_prim=False):
 base=0x08804000
 va=lambda offset:base+offset-0x100
 data=bytearray(0xa00)
 words=[]
 def load(reg,value):
  words.extend([0x3c000000|(reg<<16)|((value>>16)&0xffff),0x34000000|(reg<<21)|(reg<<16)|(value&0xffff)])
 def call(addr):words.extend([0x0c000000|(addr>>2),0])
 load(4,0x041ffffc if wrap else 0x04000000);load(5,512);load(6,3);load(7,0);call(va(0x480))
 load(4,va(0x500));load(5,0);load(6,0xffffffff);load(7,0);call(va(0x488))
 load(4,0);call(va(0x490))
 load(8,0x089ff000);load(9,0xfeed600d);words.append(0xad090000)
 stop=base+len(words)*4;words.extend([0x08000000|(stop>>2),0])
 data[0x100:0x100+4*len(words)]=struct.pack('<%dI'%len(words),*words)
 module=struct.pack('<HH28s5I',0,0x100,b'WiiGECommandSmoke',0,0,0,va(0x340),va(0x368))
 data[0x280:0x280+len(module)]=module
 data[0x300:0x30b]=b'sceDisplay\0';data[0x310:0x31b]=b'sceGe_user\0'
 # PSP library import headers: name, version, flags, size/vars/functions, NIDs, stubs.
 data[0x340:0x354]=struct.pack('<IHHBBHII',va(0x300),0x100,0x4001,5,0,1,va(0x440),va(0x480))
 data[0x354:0x368]=struct.pack('<IHHBBHII',va(0x310),0x100,0x4001,5,0,2,va(0x444),va(0x488))
 data[0x440:0x44c]=struct.pack('<3I',0x289d82fe,0xab49e76a,0xb287bd61)
 data[0x480:0x498]=struct.pack('<6I',0x03e00008,0,0x03e00008,0,0x03e00008,0)
 def cmd(c,arg=0):return (c<<24)|(arg&0xffffff)
 def point(color,x,y):return struct.pack('<Ihhhxx',color,x,y,0)
 data[0x700:0x718]=point(0xff403020,0,0)+point(0xff403020,480,272)
 data[0x720:0x744]=point(0xff0000ff,80,220)+point(0xff00ff00,240,40)+point(0xffff0000,400,220)
 data[0x750:0x768]=point(0xffffffff,0,0)+point(0xffffffff,64,64)
 scissor=479|(271<<10)
 cmds=[cmd(0x9c,0x1ffffc if wrap else 0),cmd(0x9d,512),cmd(0xd2,3),cmd(0x4c,0),cmd(0x4d,0),
       cmd(0x15,0),cmd(0x16,scissor),cmd(0xd4,0),cmd(0xd5,scissor),
       cmd(0x12,0x80011c),cmd(0x10,(va(0x700)>>8)&0xff0000),cmd(0x01,va(0x700)),
       cmd(0xd3,0x301),cmd(0x04,(6<<16)|2),cmd(0xd3,0),
       cmd(0x1e,0),cmd(0x17,0),cmd(0x1d,0),cmd(0x21,0),cmd(0x22,0),cmd(0x23,0),
       cmd(0xd8,0),cmd(0xe8,0),cmd(0xe9,0),cmd(0x50,1),
       cmd(0x01,va(0x720)),cmd(0x04,(3<<16)|3),
       cmd(0x01,va(0x750)),cmd(0xd3,0x301),cmd(0x04,(6<<16)|2),cmd(0xd3,0),
       cmd(0x0f),cmd(0x0c)]
 if sprite:
  data[0x780:0x7a0]=struct.pack('<hhIhhhxxhhIhhhxx',0,0,0xffffffff,400,190,0,4,4,0xffffffff,464,254,0)
  if large_sprite:
   data[0x780:0x7a0]=struct.pack('<hhIhhhxxhhIhhhxx',0,0,0xffffffff,96,96,0,4,4,0xffffffff,352,224,0)
  for y in range(4):
   for x in range(4):struct.pack_into('<I',data,0x7a0+(y*4+x)*4,0xff0000ff if (x^y)&1 else 0xff00ff00)
  cmds=cmds[:-2]+[cmd(0x12,0x80011e),cmd(0x1e,1),cmd(0xa0,va(0x7a0)),cmd(0xa8,((va(0x7a0)>>8)&0xf0000)|4),cmd(0xb8,2|(2<<8)),cmd(0xc2,0),cmd(0xc3,3),cmd(0xc6,0),cmd(0xc7,0),cmd(0xc9,3),cmd(0x01,va(0x780)),cmd(0x04,(6<<16)|2),cmd(0x0f),cmd(0x0c)]
 if zero_prim:
  # Valid final RAM word, insufficient for even one vertex. An empty draw must
  # never touch it or underflow vertex_count-1 to 65535.
  cmds=cmds[:-2]+[cmd(0x10,0x00090000),cmd(0x01,0xfffffc),cmd(0x04,3<<16)]+cmds[-2:]
 data[0x500:0x500+len(cmds)*4]=struct.pack('<%dI'%len(cmds),*cmds)
 names=b'\0.text\0.rodata.sceModuleInfo\0.shstrtab\0'
 data[0x800:0x800+len(names)]=names
 ident=b'\x7fELF\x01\x01\x01'+bytes(9)
 data[:52]=struct.pack('<16sHH5I6H',ident,2,8,1,base,52,0x900,0x10001001,52,32,1,40,4,3)
 data[52:84]=struct.pack('<8I',1,0x100,base,0x180,0x700,0x700,7,0x100)
 sections=[(0,)*10,(1,1,6,base,0x100,len(words)*4,0,0,4,0),
  (names.index(b'.rodata'),1,2,va(0x280),0x280,len(module),0,0,4,0),
  (names.index(b'.shstrtab'),3,0,0,0x800,len(names),0,0,1,0)]
 for i,section in enumerate(sections):data[0x900+i*40:0x900+(i+1)*40]=struct.pack('<10I',*section)
 return bytes(data)
if __name__=='__main__':Path('PSP-ge-smoke.elf').write_bytes(make_elf())
