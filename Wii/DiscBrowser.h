// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace Wii {
struct DiscImage {
  std::string name;
  std::string path;
  uint64_t size;
};
struct ScanReport {
  bool folderReadable = false;
  unsigned entries = 0, folders = 0, candidates = 0, metadataFailures = 0;
  bool limited = false;
  std::string path;
  std::vector<std::string> examples;
  std::vector<std::string> problems;
  std::string Summary() const;
};
bool IsDiscFilename(const std::string &name);
bool ProbeDisc(const std::string &path, std::string *error);
std::vector<DiscImage> ScanDiscs(const std::string &directory, std::string *error,
  ScanReport *report = nullptr, bool recursive = true);
std::string DirectorySummary(const std::string &directory);
std::string DisplayFilename(const std::string &name, size_t limit);
}
