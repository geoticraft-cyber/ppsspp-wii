// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>
namespace Wii {
// Switch only between games, with input/audio workers stopped. Never removes
// existing logs when disabled. Returns false if enabling cannot open all files.
bool SetFileLogging(const std::string &root,bool enabled,unsigned detail);
void CloseFileLogging();
void FlushFileLogging();
}
