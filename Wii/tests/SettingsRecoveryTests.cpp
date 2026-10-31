// SPDX-License-Identifier: GPL-2.0-or-later
#include "Wii/Settings.h"
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>

static bool failRotation = false, failPublish = false;
extern "C" int __real_rename(const char *, const char *);
extern "C" int __wrap_rename(const char *from, const char *to) {
  const std::string source(from), destination(to);
  if ((failRotation && destination == "recovery.ini.bak") ||
      (failPublish && source == "recovery.ini.tmp")) {
    errno = EIO;
    return -1;
  }
  return __real_rename(from, to);
}
static void Write(const char *path, const std::string &text) {
  FILE *file = std::fopen(path, "w");
  assert(file);
  assert(std::fwrite(text.data(), 1, text.size(), file) == text.size());
  assert(std::fclose(file) == 0);
}
static void ExpectSaved(bool logging) {
  Wii::GetSettings() = Wii::Settings();
  assert(Wii::LoadSettings("recovery.ini"));
  assert(Wii::GetSettings().logging == logging);
}
int main() {
  auto &s = Wii::GetSettings();
  s = Wii::Settings();
  s.logging = true;
  assert(Wii::SaveSettings("recovery.ini"));
  // Simulate power loss after primary -> backup, before temp -> primary.
  assert(std::rename("recovery.ini", "recovery.ini.bak") == 0);
  Write("recovery.ini.tmp", "logging=0\n");
  ExpectSaved(true);
  failPublish = true;
  assert(!Wii::SaveSettings("recovery.ini"));
  failPublish = false;
  ExpectSaved(true);  // A failed recovery save must retain the only backup.
  assert(Wii::SaveSettings("recovery.ini"));

  s.logging = false;
  failRotation = true;
  assert(!Wii::SaveSettings("recovery.ini"));
  failRotation = false;
  ExpectSaved(true);  // Rotation failure must never replace the primary.
  s.logging = false;
  failPublish = true;
  assert(!Wii::SaveSettings("recovery.ini"));
  failPublish = false;
  ExpectSaved(true);  // Publishing failure restores the original primary.

  // Overflow, junk, oversized lines and comments must not change options.
  Write("recovery.ini", "  logging = 0 ; comment\r\n"
    "video=999999999999999999999999999999999999\n"
    "gx=0oops\n" + std::string(127, 'x') + "gx=0\n"
    "#gx=0\n;gx=0\n  psp_clock_mhz = 168 # comment\n"
    "diagnostic_level=-999999999999999999999999999999\n");
  s = Wii::Settings();
  assert(Wii::LoadSettings("recovery.ini"));
  assert(!s.logging && s.gx && s.video == 0 && s.pspClockMHz == 168 && s.diagnosticLevel == 1);
  std::remove("recovery.ini");
  std::remove("recovery.ini.bak");
  std::remove("recovery.ini.tmp");
  s = Wii::Settings();
  assert(!Wii::LoadSettings("recovery.ini"));
  puts("pass: interrupted-save recovery, failed renames, integer bounds and whole-line parsing");
}
