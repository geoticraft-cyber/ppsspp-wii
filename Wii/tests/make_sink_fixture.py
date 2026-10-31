#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Original guest program: reserve, submit twice, wait/drain and release."""
from pathlib import Path
import struct,sys
from make_ge_smoke_elf import make_elf
s=bytearray(make_elf());base=0x08804000
va=lambda n:base+n-0x100
words=[]
def load(r,v):words.extend([0x3c000000|r<<16|(v>>16)&65535,0x34000000|r<<21|r<<16|v&65535])
def call(a):words.extend([0x0c000000|a>>2,0])
load(4,0xffffffff);load(5,64);load(6,0);call(va(0x480))
for _ in range(2):
 load(4,0);load(5,32768);load(6,0x08aff000);call(va(0x488))
load(4,0);call(va(0x490))
load(8,0x089ff000);load(9,0xfeed600d);words.append(0xad090000)
stop=base+len(words)*4;words.extend([0x08000000|stop>>2,0])
s[0x100:0x100+len(words)*4]=struct.pack('<%dI'%len(words),*words)
for offset in [0x300,0x310]:s[offset:offset+12]=b'sceAudio\0\0\0\0'
s[0x440:0x44c]=struct.pack('<3I',0x5ec81c55,0x136caf51,0x6fc46853)
# Enlarge the text section to include every instruction in this fixture.
struct.pack_into('<I',s,0x900+40+20,len(words)*4)
Path(sys.argv[1]).write_bytes(s)
