#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build the real Wii portable core and run guest I/O regressions without a Wii SDK.
Requires g++, gcc, Python 3, zlib headers/library and pthreads. Uses bundled PNG.
"""
import concurrent.futures, subprocess, pathlib, re, sys, os
root=pathlib.Path(__file__).resolve().parents[2]; out=root/'build-io-host';out.mkdir(exist_ok=True)
sources=re.findall(r'^  ([^#\s]+\.(?:cpp|c))$',(root/'Wii/Sources.cmake').read_text(),re.M)
sources+=['Wii/FileLogging.cpp','Wii/AllocationDiagnostics.cpp','Wii/Memory.cpp','Wii/System.cpp','Wii/Version.cpp','Wii/Settings.cpp','Wii/tests/CoreIntegration.cpp','Wii/tests/HostMemoryUtil.cpp','ext/snappy/snappy.cpp','ext/snappy/snappy-c.cpp']
sources += [str(p.relative_to(root)) for p in (root/'ext/native/ext/libpng17').glob('*.c') if p.name!='pngtest.c']
flags=['-pipe','-Os','-ffunction-sections','-fdata-sections','-fno-strict-aliasing','-DHW_RVL','-DPPSSPP_WII_PORTABLE_TEST','-DNO_VULKAN','-DMOBILE_DEVICE','-D__STDC_CONSTANT_MACROS','-DSHARED_ZLIB','-DMAX_LOGLEVEL=5','-I.','-ICommon','-Iext/native','-IWii/tests/compat','-Iext/native/ext','-Iext/native/ext/libzip','-Iext/snappy','-Iext/native/ext/libpng17']
if os.environ.get("WII_PROFILE_BUILD"):flags += ["-DWII_CPU_PROFILE"]
def build(src):
 obj=out/(src.replace('/','_')+'.o');log=obj.with_suffix('.log')
 cxx=src.endswith('.cpp');cmd=['g++' if cxx else 'gcc']+flags+(['-std=c++11','-fpermissive','-include','Wii/tests/HostConfig.h'] if cxx else [])+['-c',src,'-o',str(obj)]
 p=subprocess.run(cmd,cwd=root,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True);log.write_text(p.stdout)
 if p.returncode:print('FAIL',src, p.stdout[-3000:],flush=True);return None
 return obj
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
 objs=list(pool.map(build,sources))
if None in objs:sys.exit(1)
p=subprocess.run(['g++','-Wl,--gc-sections']+[str(x) for x in objs]+['-lz','-lpthread','-o',str(out/'WiiCoreIntegration')],cwd=root,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
(out/'link.log').write_text(p.stdout);print(p.stdout[-6000:]);print('link status',p.returncode,flush=True)
if p.returncode:sys.exit(p.returncode)
import os
run=out/'test-run';run.mkdir(exist_ok=True)
mem=run/'test-memstick';mem.mkdir(exist_ok=True)
(mem/'io-regression.bin').write_bytes(b'abcdefghijklmnop')
subprocess.run([sys.executable,str(root/'Wii/tests/make_smoke_elf.py'),str(run/'smoke.elf')],check=True)
subprocess.run([sys.executable,str(root/'Wii/tests/make_sink_fixture.py'),str(run/'sink.elf')],check=True)
for mode in ('io','smoke','dispatch','services','guest-waits'):
 env=os.environ.copy()
 env.pop('WII_TEST_IO',None);env.pop('WII_TEST_DISPATCH',None);env.pop('WII_TEST_COMPAT',None);env.pop('WII_TEST_SILENT',None);env.pop('WII_TEST_SINK_GUEST',None)
 if mode=='io':env['WII_TEST_IO']='1'
 if mode=='dispatch':env['WII_TEST_DISPATCH']='1'
 if mode=='services':env['WII_TEST_SILENT']='1'
 if mode=='guest-waits':env['WII_TEST_SINK_GUEST']='1'
 with (out/(mode+'-test.log')).open('w') as log:
  subprocess.run([str(out/'WiiCoreIntegration'),'sink.elf' if mode=='guest-waits' else 'smoke.elf',str(root/'assets')],cwd=run,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
 print('pass: '+mode,flush=True)
