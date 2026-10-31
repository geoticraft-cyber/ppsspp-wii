// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#pragma once
#include "ppsspp_config.h"

// Abstractions around read-only blockdevices, such as PSP UMD discs.
// CISOFileBlockDevice implements compressed iso images, CISO format.
//
// The ISOFileSystemReader reads from a BlockDevice, so it automatically works
// with CISO images.

#include <mutex>
#include <vector>
#include "zlib.h"

#include "Common/CommonTypes.h"
#include "Core/ELF/PBPReader.h"

class FileLoader;

class BlockDevice {
public:
	virtual ~BlockDevice() {}
	virtual bool ReadBlock(int blockNumber, u8 *outPtr, bool uncached = false) = 0;
	virtual bool ReadBlocks(u32 minBlock, int count, u8 *outPtr) {
		for (int b = 0; b < count; ++b) {
			if (!ReadBlock(minBlock + b, outPtr)) {
				return false;
			}
			outPtr += GetBlockSize();
		}
		return true;
	}
	int GetBlockSize() const { return 2048;}  // forced, it cannot be changed by subclasses
	virtual u32 GetNumBlocks() = 0;

	u32 CalculateCRC();
};

class CISOFileBlockDevice : public BlockDevice {
public:
	CISOFileBlockDevice(FileLoader *fileLoader);
	~CISOFileBlockDevice();
	bool ReadBlock(int blockNumber, u8 *outPtr, bool uncached = false) override;
	bool ReadBlocks(u32 minBlock, int count, u8 *outPtr) override;
	u32 GetNumBlocks() override { return numBlocks; }

private:
  bool ReadBlockLocked(int blockNumber,u8 *outPtr,bool uncached);
  // One serialized decoder per device. Reset after success or failure; never
  // dereference a previous caller's destination after resetting the stream.
  z_stream inflater_{};
  bool inflaterReady_ = false;
  std::recursive_mutex readMutex_;
  z_stream *ResetInflater();
	bool ReadIndex(u32 entry, u32 *value);
	FileLoader *fileLoader_;
#if PPSSPP_PLATFORM(WII)
	// RECOVERED_1_2: target object layout uses a 4096-entry page plus one
	// overlap entry instead of retaining the entire CSO index.
	u32 indexBase = 0;
	u32 indexValid = 0;
	u32 indexCapacity = 0;
#endif
	u32 *index = nullptr;
	u8 *readBuffer = nullptr;
	u8 *zlibBuffer = nullptr;
	u32 zlibBufferFrame = 0;
	u8 indexShift = 0;
	u8 blockShift = 0;
	u32 frameSize = 0;
	u32 numBlocks = 0;
	u32 numFrames = 0;
};


class FileBlockDevice : public BlockDevice {
public:
	FileBlockDevice(FileLoader *fileLoader);
	~FileBlockDevice();
	bool ReadBlock(int blockNumber, u8 *outPtr, bool uncached = false) override;
	bool ReadBlocks(u32 minBlock, int count, u8 *outPtr) override;
	u32 GetNumBlocks() override {return (u32)(filesize_ / GetBlockSize());}

private:
	FileLoader *fileLoader_;
	u64 filesize_;
};


// For encrypted ISOs in PBP files.

struct table_info {
	u8 mac[16];
	u32_le offset;
	s32_le size;
	s32_le flag;
	s32_le unk_1c;
};

class NPDRMDemoBlockDevice : public BlockDevice {
public:
	NPDRMDemoBlockDevice(FileLoader *fileLoader);
	~NPDRMDemoBlockDevice();

	bool ReadBlock(int blockNumber, u8 *outPtr, bool uncached = false) override;
	u32 GetNumBlocks() override {return valid_ ? lbaSize : 0;}

private:
	FileLoader *fileLoader_;
	static std::mutex mutex_;
	bool valid_ = false;
	u32 lbaSize = 0;

	u32 psarOffset = 0;
	int blockSize = 0;
	int blockLBAs = 0;
	u32 numBlocks = 0;

	u8 vkey[16]{};
	u8 hkey[16]{};
	struct table_info *table = nullptr;

	int currentBlock = -1;
	u8 *blockBuf = nullptr;
	u8 *tempBuf = nullptr;
};


BlockDevice *constructBlockDevice(FileLoader *fileLoader);
