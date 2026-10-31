#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='performance-cso-',dir=root) as t:
 exe=Path(t)/'test'
 subprocess.run(['g++','-std=c++11','-O2','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-fsanitize='+('address,undefined' if '--asan' in sys.argv else 'undefined'),'-fno-sanitize-recover=all','-DHW_RVL','-DPPSSPP_WII_PORTABLE_TEST','-include','Wii/tests/HostConfig.h','-I.','-ICommon','-Iext/native','Core/FileSystems/BlockDevices.cpp','Wii/tests/CSOPerformanceTests.cpp','-Wl,--wrap=inflateInit2_,--wrap=inflateReset,--wrap=inflateEnd','-lz','-pthread','-o',str(exe)],cwd=root,check=True)
 subprocess.run([str(exe)],cwd=root,check=True)
