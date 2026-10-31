#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise actual settings row rendering and adjustment after row removal."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'Wii/Main.cpp').read_text()
a=s.index('static const char *SettingOnOff');b=s.index('static bool TestVideoMode',a)
with tempfile.TemporaryDirectory(prefix='settings-menu-',dir=Path.cwd()) as folder:
    cpp=Path(folder)/'test.cpp'
    cpp.write_text('''#include "Wii/Settings.h"
#include <string>
#include <vector>
#include <algorithm>
#include <cassert>
#include <cstdio>
static std::vector<std::string> rows;
void WiiMenuBegin(){} void WiiMenuEnd(){}
void WiiMenuRect(int,int,int,int,unsigned){}
void WiiMenuText(int x,int y,const char *text,unsigned,int=1){if(x==34&&y>=96&&y<=366)rows.push_back(text);}
'''+s[a:b]+'''
static std::vector<std::string> Labels(){
 rows.clear();DrawSettingsScreen(0,"");DrawSettingsScreen(10,"");return rows;
}
int main(){
 auto before=Labels();assert(before.size()==17&&before[16]=="Save and back");
 for(const auto &label:before)assert(label.find("Audio")==std::string::npos&&label.find("JIT")==std::string::npos&&label.find("CPU                     ")==std::string::npos&&label.find("rameskip")==std::string::npos&&label.find("speed target")==std::string::npos);
 for(unsigned i=0;i<16;++i){
  const auto old=Labels();AdjustSetting(i,1);const auto now=Labels();assert(now.size()==17);
  for(unsigned j=0;j<17;++j)assert((old[j]!=now[j])==(i==j));
 }
 puts("pass: actual 17-row settings menu; each selectable setting changes its own visible row, with frameskip removed");
}
''')
    exe=Path(folder)/'test'
    subprocess.run(['g++','-std=c++11','-O2','-I'+str(root),str(cpp),str(root/'Wii/Settings.cpp'),'-o',str(exe)],check=True)
    subprocess.run([str(exe.resolve())],check=True)
