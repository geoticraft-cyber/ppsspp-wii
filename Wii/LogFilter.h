// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cctype>
namespace Wii {
inline bool HiddenServiceText(const char *s){
  const char *words[]={"audio","sound","sceSas","sceAtrac","sceMp3","sceAac"};
  for(const char *p=s;*p;++p)for(const char *word:words){
    unsigned i=0;while(word[i]&&p[i]&&std::tolower(static_cast<unsigned char>(p[i]))==
      std::tolower(static_cast<unsigned char>(word[i])))++i;
    if(!word[i])return true;
  }return false;
}
}
