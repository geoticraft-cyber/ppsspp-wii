// SPDX-License-Identifier: GPL-2.0-or-later
#include "Wii/Settings.h"

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cctype>
#include <sys/stat.h>

namespace Wii {
// Recovered frontend defaults plus persisted development options.
static Settings settings;

Settings &GetSettings() { return settings; }

void ValidateSettings(Settings &s) {
  // Reject values outside the supported menu choices, including stale experiments.
  if (s.aspect < 0 || s.aspect > 1) s.aspect = 1;
  if (s.video < 0 || s.video > 4 || s.video==3) s.video = 0;
  if (s.pspClockMHz != 0 && (s.pspClockMHz < 135 || s.pspClockMHz > 333 ||
      (333 - s.pspClockMHz) % 33 != 0)) s.pspClockMHz = 0;
  if(s.cpuProfileStride!=1&&s.cpuProfileStride!=8&&s.cpuProfileStride!=64&&s.cpuProfileStride!=256)s.cpuProfileStride=64;
  if(s.diagnosticLevel<0||s.diagnosticLevel>3)s.diagnosticLevel=1;
  s.gx = !!s.gx;
  s.skipMovies = !!s.skipMovies;
  s.skipBufferEffects = !!s.skipBufferEffects;
  s.vertexCache = !!s.vertexCache;
  s.offlineHLE = !!s.offlineHLE;
}

int StepPSPClock(int mhz, int direction) {
  const int clocks[]={0,333,300,267,234,201,168,135};
  unsigned index=0;
  while(index<7 && clocks[index]!=mhz)++index;
  if(clocks[index]!=mhz)index=0;
  return clocks[(int(index)+(direction<0?-1:1)+8)%8];
}

const char *VideoModeName(int video) {
  // Exact target labels.
  static const char *const names[] = {
    "Auto", "480i / 60 Hz", "480p / 60 Hz", "Removed (Auto)", "576p / 50 Hz",
  };
  return video >= 0 && video < 5 ? names[video] : names[0];
}

static bool ReadSettings(const std::string &path, Settings &loaded) {
  FILE *fp = std::fopen(path.c_str(), "r");
  if (!fp) return false;
  Settings candidate = loaded;
  char line[128];
  while (std::fgets(line, sizeof(line), fp)) {
    // Discard the whole oversized line, including anything resembling another
    // setting in its tail. Never pass an out-of-range integer to scanf.
    if (!std::strchr(line, '\n') && !std::feof(fp)) {
      int ch;
      do { ch = std::fgetc(fp); } while (ch != '\n' && ch != EOF);
      continue;
    }
    char *key = line;
    while (std::isspace(static_cast<unsigned char>(*key))) ++key;
    if (!*key || *key == '#' || *key == ';') continue;
    char *equals = std::strchr(key, '=');
    if (!equals) continue;
    char *keyEnd = equals;
    while (keyEnd > key && std::isspace(static_cast<unsigned char>(keyEnd[-1]))) --keyEnd;
    if (keyEnd == key || keyEnd - key > 63) continue;
    *keyEnd = 0;
    char *end = nullptr;
    errno = 0;
    const long parsed = std::strtol(equals + 1, &end, 10);
    if (end == equals + 1 || errno == ERANGE || parsed < INT_MIN || parsed > INT_MAX) continue;
    while (std::isspace(static_cast<unsigned char>(*end))) ++end;
    if (*end && *end != '#' && *end != ';') continue;
    const int value = static_cast<int>(parsed);
    if (!std::strcmp(key,"cpu_profile")) candidate.cpuProfile=value!=0;
    else if (!std::strcmp(key,"cpu_profile_stride")) candidate.cpuProfileStride=value;
    else if (!std::strcmp(key, "logging")) candidate.logging = value != 0;
    else if (!std::strcmp(key, "performance_overlay")) candidate.performanceOverlay = value != 0;
    else if (!std::strcmp(key, "aspect")) candidate.aspect = value;
    else if (!std::strcmp(key, "video")) candidate.video = value;
    else if (!std::strcmp(key, "psp_clock_mhz")) candidate.pspClockMHz = value;
    else if (!std::strcmp(key, "gx_texture_cache")) candidate.gxTextureCache = value != 0;
    else if (!std::strcmp(key, "cpu_sprite_cache")) candidate.cpuSpriteCache = value != 0;
    else if (!std::strcmp(key, "low_memory")) candidate.lowMemory = value != 0;
    else if (!std::strcmp(key, "gx_advanced_sprites")) candidate.gxAdvancedSprites = value != 0;
    else if (!std::strcmp(key, "gx_sprites")) candidate.gxSprites = value != 0;
    else if (!std::strcmp(key, "gx_linear")) candidate.gxLinear = value != 0;
    else if (!std::strcmp(key, "diagnostic_level")) candidate.diagnosticLevel = value;
    else if (!std::strcmp(key, "gx")) candidate.gx = value != 0;
    else if (!std::strcmp(key, "skip_movies")) candidate.skipMovies = value != 0;
    else if (!std::strcmp(key, "skip_buffer_effects")) candidate.skipBufferEffects = value != 0;
    else if (!std::strcmp(key, "vertex_cache")) candidate.vertexCache = value != 0;
    else if (!std::strcmp(key, "offline_hle")) candidate.offlineHLE = value != 0;
  }
  const bool readOK = !std::ferror(fp);
  const bool closed = std::fclose(fp) == 0;
  if (!readOK || !closed) return false;
  ValidateSettings(candidate);
  loaded = candidate;
  return true;
}

bool LoadSettings(const std::string &path) {
  // A power loss between the two save renames leaves only the backup. Never
  // load .tmp: its write may have been interrupted before it was complete.
  if (ReadSettings(path, settings) || ReadSettings(path + ".bak", settings))
    return true;
  ValidateSettings(settings);
  return false;
}

bool SaveSettings(const std::string &path) {
  ValidateSettings(settings);
  const std::string temp = path + ".tmp";
  const std::string backup = path + ".bak";
  FILE *fp = std::fopen(temp.c_str(), "w");
  if (!fp) return false;
  // Preserve recovered key names/order and append development options.
  const int result = std::fprintf(fp,
    "performance_overlay=%d\n"
    "aspect=%d\n"
    "video=%d\n"
    "gx=%d\n"
    "skip_movies=%d\n"
    "skip_buffer_effects=%d\n"
    "vertex_cache=%d\n"
    "offline_hle=%d\n"
    "psp_clock_mhz=%d\nlow_memory=%d\ngx_sprites=%d\ngx_advanced_sprites=%d\ngx_linear=%d\ngx_texture_cache=%d\ncpu_sprite_cache=%d\ndiagnostic_level=%d\nlogging=%d\ncpu_profile=%d\ncpu_profile_stride=%d\n",
    settings.performanceOverlay ? 1 : 0, settings.aspect, settings.video,
    settings.gx ? 1 : 0, settings.skipMovies ? 1 : 0,
    settings.skipBufferEffects ? 1 : 0,
    settings.vertexCache ? 1 : 0, settings.offlineHLE ? 1 : 0,
    settings.pspClockMHz, settings.lowMemory, settings.gxSprites, settings.gxAdvancedSprites, settings.gxLinear,settings.gxTextureCache,settings.cpuSpriteCache,settings.diagnosticLevel,settings.logging,settings.cpuProfile,settings.cpuProfileStride);
  // Always close the handle, including a failed write/flush.
  const bool flushed = std::fflush(fp) == 0;
  const bool closed = std::fclose(fp) == 0;
  const bool ok = result > 0 && flushed && closed;
  if (!ok) {
    std::remove(temp.c_str());
    return false;
  }
  // Keep the last good copy when recovering a session with no primary file.
  // If rotation fails, do not overwrite the primary or discard the new .tmp
  // until the error has been handled.
  struct stat info{};
  const bool hadPrimary = stat(path.c_str(), &info) == 0;
  if (!hadPrimary && errno != ENOENT) {
    std::remove(temp.c_str());
    return false;
  }
  if (hadPrimary) {
    if ((std::remove(backup.c_str()) != 0 && errno != ENOENT) ||
        std::rename(path.c_str(), backup.c_str()) != 0) {
      std::remove(temp.c_str());
      return false;
    }
  }
  if (std::rename(temp.c_str(), path.c_str()) != 0) {
    if (hadPrimary) std::rename(backup.c_str(), path.c_str());
    std::remove(temp.c_str());
    return false;
  }
  return true;
}
}  // namespace Wii
