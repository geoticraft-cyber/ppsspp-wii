// SPDX-License-Identifier: GPL-2.0-or-later
#include "Core/Loaders.h"
#include "Core/FileSystems/BlockDevices.h"
#include "Common/Log.h"
#include "Wii/DiscBrowser.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <vector>

void GenericLog(LogTypes::LOG_LEVELS, LogTypes::LOG_TYPE, const char *, int, const char *, ...) {}
bool GenericLogEnabled(LogTypes::LOG_LEVELS, LogTypes::LOG_TYPE) { return false; }

class MemoryFile final : public FileLoader {
public:
  std::vector<u8> bytes;
  explicit MemoryFile(const char *path) {
    std::ifstream file(path, std::ios::binary);
    bytes.assign(std::istreambuf_iterator<char>(file), {});
  }
  bool Exists() override { return true; }
  bool IsDirectory() override { return false; }
  s64 FileSize() override { return bytes.size(); }
  std::string Path() const override { return "fixture"; }
  size_t ReadAt(s64 pos, size_t unit, size_t count, void *dest, Flags = Flags::NONE) override {
    if (pos < 0 || unit == 0 || count > std::numeric_limits<size_t>::max() / unit || static_cast<u64>(pos) >= bytes.size()) return 0;
    const size_t n = std::min(unit * count, bytes.size() - static_cast<size_t>(pos));
    memcpy(dest, bytes.data() + pos, n); return n / unit;
  }
};
int main(int argc, char **argv) {
  assert(argc == 2);
  const std::string root = argv[1], isoPath = root + "/PSP-smoke.iso", csoPath = root + "/PSP-smoke.cso";
  std::string error;
  auto games = Wii::ScanDiscs(root, &error);
  assert(error.empty() && games.size() == 2);
  assert(Wii::IsDiscFilename("GAME.ISO") && Wii::IsDiscFilename("game.CsO"));
  assert(!Wii::IsDiscFilename("game.iso.bak") && !Wii::IsDiscFilename(".iso"));
  assert(Wii::DisplayFilename("bad\x1b[2J.iso", 30).find('\x1b') == std::string::npos);
  assert(Wii::ProbeDisc(isoPath, &error)); assert(Wii::ProbeDisc(csoPath, &error));
  assert(!Wii::ProbeDisc(root + "/missing.iso", &error) && !error.empty());
  assert(Wii::ScanDiscs("", &error).empty() && !error.empty());
  MemoryFile iso(isoPath.c_str()), cso(csoPath.c_str());
  FileBlockDevice plain(&iso);
  CISOFileBlockDevice compressed(&cso);
  assert(compressed.GetNumBlocks() == plain.GetNumBlocks() && compressed.GetNumBlocks() > 16);
  std::vector<u8> block(2048), all(iso.bytes.size());
  for (u32 i = 0; i < compressed.GetNumBlocks(); ++i) {
    assert(compressed.ReadBlock(i, block.data()));
    assert(!memcmp(block.data(), iso.bytes.data() + i * 2048, 2048));
  }
  assert(compressed.ReadBlocks(0, compressed.GetNumBlocks(), all.data()));
  assert(all == iso.bytes);
  assert(!compressed.ReadBlock(-1, block.data()));
  assert(!compressed.ReadBlocks(compressed.GetNumBlocks() - 1, 2, block.data()));
  for (int mutation = 0; mutation < 5; ++mutation) {
    MemoryFile bad = cso;
    if (mutation == 0) bad.bytes[16] = bad.bytes[17] = bad.bytes[18] = bad.bytes[19] = 0;
    if (mutation == 1) bad.bytes[21] = 255;
    if (mutation == 2) bad.bytes[20] = 2;
    if (mutation == 3) bad.bytes.resize(25);
    if (mutation == 4) bad.bytes[24] = bad.bytes[25] = bad.bytes[26] = bad.bytes[27] = 0;
    CISOFileBlockDevice invalid(&bad);
    assert(invalid.GetNumBlocks() == 0);
    assert(!invalid.ReadBlock(0, block.data()));
  }
  puts("PASS: ISO/CSO sectors, multi-sector reads, malformed CSO headers/indexes, SD browser filtering");
}
