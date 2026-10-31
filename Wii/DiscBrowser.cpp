// SPDX-License-Identifier: GPL-2.0-or-later
#include "Wii/DiscBrowser.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>

namespace Wii {
static unsigned char FoldASCII(unsigned char c) {
  return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}
static int CompareNames(const std::string &a, const std::string &b) {
  // Compare in place: sorting thousands of long FAT filenames must not
  // allocate two lowercase copies on every comparison.
  const size_t length = std::min(a.size(), b.size());
  for (size_t i = 0; i < length; ++i) {
    const unsigned char x = FoldASCII(static_cast<unsigned char>(a[i]));
    const unsigned char y = FoldASCII(static_cast<unsigned char>(b[i]));
    if (x != y) return x < y ? -1 : 1;
  }
  return a.size() < b.size() ? -1 : a.size() > b.size() ? 1 : 0;
}
bool IsDiscFilename(const std::string &name) {
  if (name.size() <= 4) return false;
  const size_t offset = name.size() - 4;
  return name[offset] == '.' &&
    (FoldASCII(name[offset + 1]) == 'i' || FoldASCII(name[offset + 1]) == 'c') &&
    FoldASCII(name[offset + 2]) == 's' && FoldASCII(name[offset + 3]) == 'o';
}
std::string ScanReport::Summary() const {
  char text[180];
  snprintf(text, sizeof(text), "%u entries / %u folders / %u disc images%s",
    entries, folders, candidates, limited ? " (scan limit reached)" : "");
  return text;
}
std::string DirectorySummary(const std::string &directory) {
  DIR *dir = opendir(directory.c_str());
  if (!dir) return "Cannot read " + directory + ": " + strerror(errno);
  std::string result = "Root contains: ";
  unsigned count = 0;
  while (auto *entry = readdir(dir)) {
    std::string name = entry->d_name;
    if (name == "." || name == "..") continue;
    if (count++ < 8) result += (count == 1 ? "" : ", ") + name;
  }
  closedir(dir);
  if (!count) result += "[empty]";
  return result;
}
std::vector<DiscImage> ScanDiscs(const std::string &directory, std::string *error,
    ScanReport *report, bool recursive) {
  error->clear();
  ScanReport local;
  ScanReport &r = report ? *report : local;
  r = ScanReport{}; r.path = directory;
  std::vector<DiscImage> result;
  if (directory.empty()) { *error = "No game folder specified"; return result; }
  struct Pending { std::string path; unsigned depth; };
  std::vector<Pending> pending{{directory,0}};
  while (!pending.empty() && r.entries < 16384 && r.folders < 1024) {
    Pending current = pending.back(); pending.pop_back();
    DIR *dir = opendir(current.path.c_str());
    if (!dir) {
      const std::string issue = current.path + ": " + strerror(errno);
      if (r.problems.size() < 12) r.problems.push_back(issue);
      if (!current.depth) *error = "Cannot open " + issue;
      continue;
    }
    if (!current.depth) r.folderReadable = true;
    ++r.folders;
    while (r.entries < 16384) {
      errno = 0;
      dirent *entry = readdir(dir);
      if (!entry) {
        // libfat reports ENOENT at normal directory EOF.
        if (errno && errno != ENOENT && r.problems.size() < 12)
          r.problems.push_back("Directory read: " + std::string(strerror(errno)));
        break;
      }
      const std::string name = entry->d_name;
      if (name == "." || name == "..") continue;
      ++r.entries;
      if (r.examples.size() < 8) r.examples.push_back(name);
      const std::string path = current.path + (current.path.back()=='/' ? "" : "/") + name;
      struct stat info{};
      bool metadata = false;
#ifdef _DIRENT_HAVE_D_STAT
      // newlib/libfat supplies the metadata with the directory entry.
      info = entry->d_stat; metadata = info.st_mode != 0;
#endif
      if (!metadata) metadata = stat(path.c_str(), &info) == 0;
      if (!metadata) ++r.metadataFailures;
      const bool isDirectory = (metadata && S_ISDIR(info.st_mode)) || entry->d_type == DT_DIR;
      if (isDirectory) {
        if (recursive && current.depth < 4 && pending.size() < 1024)
          pending.push_back({path,current.depth+1});
        else if (recursive) r.limited = true;
        continue;
      }
      if (!IsDiscFilename(name)) continue;
      // Keep matching names visible even if a second stat/size query fails.
      // ProbeDisc reports unreadable/truncated files when the user launches.
      uint64_t size = metadata && info.st_size > 0 ? uint64_t(info.st_size) : 0;
      if (!metadata) {
        FILE *file = fopen(path.c_str(), "rb");
        if (file) {
          if (fseek(file,0,SEEK_END)==0) { long n=ftell(file); if(n>0) size=uint64_t(n); }
          fclose(file);
        }
      }
      result.push_back({name,path,size}); ++r.candidates;
      if (result.size() >= 4096) { r.limited = true; break; }
    }
    closedir(dir);
    if (result.size() >= 4096) break;
  }
  if (!pending.empty()) r.limited = true;
  std::sort(result.begin(), result.end(), [](const DiscImage &a, const DiscImage &b) {
    const int order = CompareNames(a.name, b.name);
    return order != 0 ? order < 0 : a.path < b.path;
  });
  return result;
}
static uint32_t LE32(const unsigned char *p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
bool ProbeDisc(const std::string &path, std::string *error) {
  error->clear();
  FILE *file = fopen(path.c_str(), "rb");
  if (!file) { *error = std::string("Cannot read game: ") + strerror(errno); return false; }
  unsigned char header[24]{};
  if (fread(header, 1, sizeof(header), file) != sizeof(header)) {
    *error = "Game image is empty or truncated"; fclose(file); return false;
  }
  bool valid = false;
  if (!memcmp(header, "CISO", 4)) {
    const uint32_t blockSize = LE32(header + 16);
    const uint64_t total = uint64_t(LE32(header + 8)) | uint64_t(LE32(header + 12)) << 32;
    // Bound the legacy decompressor's shifts, allocations, and index table.
    valid = header[20] == 1 && header[21] <= 16 && blockSize >= 2048 && blockSize <= 65536 &&
      (blockSize & (blockSize - 1)) == 0 && total >= 2048 && total <= 0x80000000ULL && total % 2048 == 0;
    if (valid) {
      valid = fseek(file,0,SEEK_END)==0;
      const long length = valid ? ftell(file) : -1;
      valid = length >= 0 && static_cast<uint64_t>(length) >=
        24 + ((total + blockSize - 1) / blockSize + 1) * 4;
    }
    if (!valid) *error = "Invalid/unsupported CSO header (requires CSO v1)";
  } else {
    unsigned char pvd[7]{};
    valid = fseek(file, 16 * 2048, SEEK_SET) == 0 && fread(pvd, 1, 7, file) == 7 &&
      pvd[0] == 1 && !memcmp(pvd + 1, "CD001", 5) && pvd[6] == 1;
    if (!valid) *error = "Not a valid ISO9660 PSP image";
  }
  fclose(file);
  return valid;
}
std::string DisplayFilename(const std::string &name, size_t limit) {
  std::string result = name.substr(0, limit);
  for (char &c : result) if (static_cast<unsigned char>(c) < 32 || c == 127) c = '?';
  if (name.size() > limit && limit >= 3) result.replace(limit - 3, 3, "...");
  return result;
}
}
