#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Original smoke disc-directory fixtures with selected compatibility IDs; no retail game data."""
from pathlib import Path
import struct,sys
from make_smoke_elf import make_elf

def sfo(disc_id):
 values={'BOOTABLE':1,'CATEGORY':'UG','DISC_ID':disc_id,'PSP_SYSTEM_VER':'1.00','TITLE':'rc11 compatibility smoke'}
 keys,data,entries=bytearray(),bytearray(),[]
 for name,value in sorted(values.items()):
  raw=struct.pack('<I',value) if isinstance(value,int) else value.encode()+b'\0'
  while len(data)%4:data.append(0)
  entries.append(struct.pack('<HHIII',len(keys),0x404 if isinstance(value,int) else 0x204,len(raw),len(raw),len(data)))
  keys+=name.encode()+b'\0';data+=raw
 start=20+len(entries)*16;end=(start+len(keys)+3)&~3
 return struct.pack('<4s4I',b'\0PSF',0x101,start,end,len(entries))+b''.join(entries)+keys+bytes(end-start-len(keys))+data

def create(folder):
 folder=Path(folder);folder.mkdir(parents=True,exist_ok=True)
 for disc_id in ['UCUS98744','UCES01264','UCJS10107','NPJG00073','UCAS40262','UCUS98737','UCES01401','UCUS98653','UCES00842','ULUS10383','ULUS10160','HBWI00001']:
  game=folder/disc_id/'PSP_GAME';(game/'SYSDIR').mkdir(parents=True,exist_ok=True)
  (game/'PARAM.SFO').write_bytes(sfo(disc_id))
  (game/'SYSDIR/EBOOT.BIN').write_bytes(make_elf())
  (folder/disc_id/'UMD_DATA.BIN').write_bytes((disc_id+'|0000000000000000|0001|G').encode())

if __name__=='__main__':create(sys.argv[1])
