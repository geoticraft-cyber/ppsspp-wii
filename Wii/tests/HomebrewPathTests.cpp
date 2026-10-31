// SPDX-License-Identifier: GPL-2.0-or-later
#include "Wii/HomebrewPath.h"
#include <cassert>
#include <cstdio>
int main() {
  assert(Wii::ChovyMissingIconFallback("/PSP_GAME/USRDIR/games /ICON0.PNG")=="/PSP_GAME/USRDIR/games/ICON0.PNG");
  for (const char *path : {"", "/PSP_GAME/USRDIR/games/ICON0.PNG", "/PSP_GAME/USRDIR/games /game.ini",
      "/PSP_GAME/USRDIR/games  /ICON0.PNG", "/games /ICON0.PNG", "/PSP_GAME/USRDIR/games /../ICON0.PNG"})
    assert(Wii::ChovyMissingIconFallback(path).empty());
  puts("pass: exact Chovy disc icon alias; other paths unchanged");
}
