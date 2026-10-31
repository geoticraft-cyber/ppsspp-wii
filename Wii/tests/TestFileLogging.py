#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='file-logging-') as tmp:
 p=Path(tmp);exe=p/'test';folder=p/'logs';folder.mkdir()
 subprocess.run(['g++','-std=c++11','-O2','-fsanitize=undefined','-fno-sanitize-recover=all','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-pthread','-DPPSSPP_WII_PORTABLE_TEST','-I'+str(root),str(root/'Wii/FileLogging.cpp'),str(root/'Wii/Diagnostics.cpp'),str(root/'Wii/AllocationDiagnostics.cpp'),str(root/'Wii/tests/FileLoggingTests.cpp'),'-o',str(exe)],check=True)
 subprocess.run([str(exe),str(folder)],check=True)
main=(root/'Wii/Main.cpp').read_text();startup=main[main.index('int main('):]
assert startup.index('Wii::LoadSettings(root + "wii.ini")')<startup.index('Wii::SetFileLogging(root,')
assert 'freopen(' not in main and 'g_Config.bEnableLogging=settings.logging' in main
assert 'loggingReady=ApplyLoggingSettings(root)' in main
print('pass: startup reads logging preference before touching logs, and menu save applies it live')
