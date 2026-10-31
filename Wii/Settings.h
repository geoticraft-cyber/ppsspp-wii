// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>

namespace Wii {
// Recovered 1.2 fields retain their defaults; development options extend them.
struct Settings {
  bool performanceOverlay=false;
  bool cpuProfile=false;
  int cpuProfileStride=64;
  bool logging=false; // File logging is opt-in; detail is retained separately.
  bool gxTextureCache=true;
  bool cpuSpriteCache=true;
  int aspect = 1;
  int video = 0;
  int diagnosticLevel = 1;
  bool gx = true;
  bool skipMovies = true;
  bool skipBufferEffects = false;
  bool vertexCache = false;
  bool offlineHLE = true;
  // Development options. CPU clock changes the emulated PSP, not Broadway.
  int pspClockMHz = 0;
  bool lowMemory = false;
  bool gxSprites = true;
  bool gxAdvancedSprites = true; // Experimental destination-dependent GX path.
  bool gxLinear = false;
};

Settings &GetSettings();
void ValidateSettings(Settings &settings);
int StepPSPClock(int mhz, int direction);
const char *VideoModeName(int video);
bool LoadSettings(const std::string &path);
bool SaveSettings(const std::string &path);

inline bool SkipUnsupportedMovies() { return GetSettings().skipMovies; }
inline bool SkipBufferEffects() { return GetSettings().skipBufferEffects; }
inline bool VertexCacheHack() { return GetSettings().vertexCache; }
inline bool OfflineHLE() { return GetSettings().offlineHLE; }
}  // namespace Wii
