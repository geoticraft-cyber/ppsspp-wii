// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>
namespace Wii {
// chovy-gm ChovyUI.cs writes "games /ICON0.PNG" into game.ini, while
// copying the icon into "games". Only retry this exact missing disc asset.
inline std::string ChovyMissingIconFallback(const std::string &path) {
  if (path == "/PSP_GAME/USRDIR/games /ICON0.PNG")
    return "/PSP_GAME/USRDIR/games/ICON0.PNG";
  return std::string();
}
}
