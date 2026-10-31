#include "Wii/CpuProfile.h"
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


#include "ppsspp_config.h"
#include <cstdint>
#include "Common/FileUtil.h"
#include "Common/Swap.h"
#include "Core/Loaders.h"
#include "Core/FileSystems/BlockDevices.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <new>

extern "C"
{
#include "zlib.h"
#include "ext/libkirk/amctrl.h"
#include "ext/libkirk/kirk_engine.h"
};

std::mutex NPDRMDemoBlockDevice::mutex_;

BlockDevice *constructBlockDevice(FileLoader *fileLoader) {
	// Check for CISO
	if (!fileLoader->Exists())
		return nullptr;
	char buffer[4]{};
	size_t size = fileLoader->ReadAt(0, 1, 4, buffer);
	if (size == 4 && !memcmp(buffer, "CISO", 4))
		return new CISOFileBlockDevice(fileLoader);
	else if (size == 4 && !memcmp(buffer, "\x00PBP", 4))
		return new NPDRMDemoBlockDevice(fileLoader);
	else
		return new FileBlockDevice(fileLoader);
}

u32 BlockDevice::CalculateCRC() {
	u32 crc = crc32(0, Z_NULL, 0);

	u8 block[2048];
	for (u32 i = 0; i < GetNumBlocks(); ++i) {
		if (!ReadBlock(i, block, true)) {
			ERROR_LOG(FILESYS, "Failed to read block for CRC");
			return 0;
		}
		crc = crc32(crc, block, 2048);
	}

	return crc;
}


FileBlockDevice::FileBlockDevice(FileLoader *fileLoader)
	: fileLoader_(fileLoader) {
	filesize_ = fileLoader->FileSize();
}

FileBlockDevice::~FileBlockDevice() {
}

bool FileBlockDevice::ReadBlock(int blockNumber, u8 *outPtr, bool uncached) {
	FileLoader::Flags flags = uncached ? FileLoader::Flags::HINT_UNCACHED : FileLoader::Flags::NONE;
	if (fileLoader_->ReadAt((u64)blockNumber * (u64)GetBlockSize(), 1, 2048, outPtr, flags) != 2048) {
		DEBUG_LOG(FILESYS, "Could not read 2048 bytes from block");
		return false;
	}

	return true;
}

bool FileBlockDevice::ReadBlocks(u32 minBlock, int count, u8 *outPtr) {
	if (fileLoader_->ReadAt((u64)minBlock * (u64)GetBlockSize(), 2048, count, outPtr) != (size_t)count) {
		ERROR_LOG(FILESYS, "Could not read %d bytes from block", 2048 * count);
		return false;
	}
	return true;
}

// .CSO format

// compressed ISO(9660) header format
typedef struct ciso_header
{
	unsigned char magic[4];         // +00 : 'C','I','S','O'
	u32_le header_size;             // +04 : header size (==0x18)
	u64_le total_bytes;             // +08 : number of original data size
	u32_le block_size;              // +10 : number of compressed block size
	unsigned char ver;              // +14 : version 01
	unsigned char align;            // +15 : align of index value
	unsigned char rsv_06[2];        // +16 : reserved
#if 0
	// INDEX BLOCK
	unsigned int index[0];          // +18 : block[0] index
	unsigned int index[1];          // +1C : block[1] index
	:
	:
	unsigned int index[last];       // +?? : block[last]
	unsigned int index[last+1];     // +?? : end of last data point
	// DATA BLOCK
	unsigned char data[];           // +?? : compressed or plain sector data
#endif
} CISO_H;


// TODO: Need much better error handling.

#if PPSSPP_PLATFORM(WII)
static const u32 CSO_READ_BUFFER_SIZE = 64 * 1024;
#else
static const u32 CSO_READ_BUFFER_SIZE = 256 * 1024;
#endif

CISOFileBlockDevice::CISOFileBlockDevice(FileLoader *fileLoader)
	: fileLoader_(fileLoader)
{
	// CISO format is fairly simple, but most tools do not write the header_size.

	CISO_H hdr{};
	const size_t readSize = fileLoader->ReadAt(0, sizeof(CISO_H), 1, &hdr);
	const u64 total = hdr.total_bytes;
	frameSize = hdr.block_size;
	if (readSize != 1 || memcmp(hdr.magic, "CISO", 4) != 0 || hdr.ver != 1 ||
		hdr.align > 16 || frameSize < 2048 || frameSize > 65536 ||
		(frameSize & (frameSize - 1)) != 0 || total < 2048 || total > 0x80000000ULL ||
		total % 2048 != 0 || fileLoader->FileSize() < static_cast<s64>(sizeof(CISO_H))) {
		ERROR_LOG(LOADER, "Invalid or unsupported CSO header");
		return;
	}
	const u64 frames = (total + frameSize - 1) / frameSize;
	if (sizeof(CISO_H) + (frames + 1) * 4 > static_cast<u64>(fileLoader->FileSize())) {
		ERROR_LOG(LOADER, "Truncated CSO index table");
		return;
	}

	// Determine the translation from block to frame.
	blockShift = 0;
	for (u32 i = frameSize; i > 0x800; i >>= 1)
		++blockShift;

	indexShift = hdr.align;
	const u64 totalSize = hdr.total_bytes;
	numFrames = (u32)((totalSize + frameSize - 1) / frameSize);
	numBlocks = (u32)(totalSize / GetBlockSize());
	VERBOSE_LOG(LOADER, "CSO numBlocks=%i numFrames=%i align=%i", numBlocks, numFrames, indexShift);

  const u32 workingBytes=frameSize+(1u<<indexShift);
  readBuffer=new (std::nothrow) u8[std::max(CSO_READ_BUFFER_SIZE,workingBytes)];
  zlibBuffer=new (std::nothrow) u8[workingBytes];
  zlibBufferFrame=numFrames;
  const u32 fullIndexEntries=numFrames+1;
#if PPSSPP_PLATFORM(WII)
  // RECOVERED_1_2: disassembly at 0x80045f94 caps the resident index at
  // 4097 entries.  Pages are aligned to 4096 entries; the extra entry keeps
  // frame N and N+1 available together at a page boundary.
  indexCapacity=std::min<u32>(fullIndexEntries,4097);
  index=new (std::nothrow) u32[indexCapacity];
  if(!readBuffer||!zlibBuffer||!index) {
    ERROR_LOG(LOADER,"Not enough memory for CSO buffers/index (%u cached entries)",indexCapacity);
    numBlocks=0;return;
  }
  indexBase=0;indexValid=0;
#else
  index=new (std::nothrow) u32[fullIndexEntries];
  if(!readBuffer||!zlibBuffer||!index) {
    ERROR_LOG(LOADER,"Not enough memory for CSO buffers/index (%u cached entries)",fullIndexEntries);
    numBlocks=0;return;
  }
  if(fileLoader->ReadAt(sizeof(hdr),sizeof(u32),fullIndexEntries,index)!=fullIndexEntries) {
    ERROR_LOG(LOADER,"Short read of CSO index");numBlocks=0;return;
  }
#if !COMMON_LITTLE_ENDIAN
  for(u32 i=0;i<fullIndexEntries;++i) {
    const u8 *p=reinterpret_cast<const u8 *>(&index[i]);
    index[i]=u32(p[0])|(u32(p[1])<<8)|(u32(p[2])<<16)|(u32(p[3])<<24);
  }
#endif
#endif
  const u64 firstData = sizeof(CISO_H) + static_cast<u64>(fullIndexEntries) * sizeof(u32);
  u32 previous=0;
  for (u32 i = 0; i < fullIndexEntries; ++i) {
    u32 value=0;
#if PPSSPP_PLATFORM(WII)
    if(!ReadIndex(i,&value)) { numBlocks=0; return; }
#else
    value=index[i];
#endif
    const u64 pos = static_cast<u64>(value & 0x7fffffff) << indexShift;
    if (pos < firstData || pos > static_cast<u64>(fileLoader->FileSize()) ||
        (i && (value & 0x7fffffff) < (previous & 0x7fffffff))) {
      ERROR_LOG(LOADER, "Invalid CSO index offset at %u", i);
      numBlocks = 0;
      return;
    }
    previous=value;
  }
#if PPSSPP_PLATFORM(WII)
  INFO_LOG(LOADER,"CSO memory: index=%u bytes (full=%u), compressed buffer=%u, frame buffer=%u",
    indexCapacity*unsigned(sizeof(u32)),fullIndexEntries*unsigned(sizeof(u32)),
    unsigned(std::max(CSO_READ_BUFFER_SIZE,workingBytes)),workingBytes);
#endif
}


bool CISOFileBlockDevice::ReadIndex(u32 entry, u32 *value) {
  if(!value || entry>numFrames || !index)return false;
#if PPSSPP_PLATFORM(WII)
  if(entry<indexBase || entry-indexBase>=indexValid) {
    const u32 pageBase=entry&~4095u;
    const u32 remaining=numFrames+1-pageBase;
    const u32 count=std::min(indexCapacity,remaining);
    indexValid=0;
    if(count==0)return false;
    const size_t got=fileLoader_->ReadAt(sizeof(CISO_H)+u64(pageBase)*sizeof(u32),sizeof(u32),count,index);
    if(got!=count) {
      ERROR_LOG(LOADER,"Short CSO index read at %u",pageBase);
      return false;
    }
#if !COMMON_LITTLE_ENDIAN
    for(u32 i=0;i<count;++i) {
      const u8 *p=reinterpret_cast<const u8 *>(&index[i]);
      index[i]=u32(p[0])|(u32(p[1])<<8)|(u32(p[2])<<16)|(u32(p[3])<<24);
    }
#endif
    indexBase=pageBase;
    indexValid=count;
  }
  *value=index[entry-indexBase];
#else
  *value=index[entry];
#endif
  return true;
}

z_stream *CISOFileBlockDevice::ResetInflater() {
  if(inflaterReady_) {
    if(inflateReset(&inflater_)==Z_OK)return &inflater_;
    inflateEnd(&inflater_);inflaterReady_=false;inflater_={};
  }
  if(inflateInit2(&inflater_,-15)!=Z_OK)return nullptr;
  inflaterReady_=true;return &inflater_;
}

CISOFileBlockDevice::~CISOFileBlockDevice()
{
  if(inflaterReady_)inflateEnd(&inflater_);
	delete [] index;
	delete [] readBuffer;
	delete [] zlibBuffer;
}

bool CISOFileBlockDevice::ReadBlock(int blockNumber, u8 *outPtr, bool uncached)
{
  Wii::CpuScope cpuLock(Wii::CpuPhase::Lock);
  std::lock_guard<std::recursive_mutex> guard(readMutex_);
  cpuLock.End();
  return ReadBlockLocked(blockNumber,outPtr,uncached);
}

// Both public entries own readMutex_; the single-sector bulk case must not
// reacquire it or dispatch virtually into the same decoder a second time.
bool CISOFileBlockDevice::ReadBlockLocked(int blockNumber,u8 *outPtr,bool uncached) {

  if(!outPtr)return false;
	FileLoader::Flags flags = uncached ? FileLoader::Flags::HINT_UNCACHED : FileLoader::Flags::NONE;
	if ((u32)blockNumber >= numBlocks)
	{
		memset(outPtr, 0, GetBlockSize());
		return false;
	}

	const u32 frameNumber = blockNumber >> blockShift;
	u32 idx=0,nextIdx=0;
	if(!ReadIndex(frameNumber,&idx)||!ReadIndex(frameNumber+1,&nextIdx)) {
		memset(outPtr,0,GetBlockSize());return false;
	}
	const u32 indexPos = idx & 0x7FFFFFFF;
	const u32 nextIndexPos = nextIdx & 0x7FFFFFFF;

	const u64 compressedReadPos = (u64)indexPos << indexShift;
	const u64 compressedReadEnd = (u64)nextIndexPos << indexShift;
	const size_t compressedReadSize = (size_t)(compressedReadEnd - compressedReadPos);
	const size_t capacity = std::max<size_t>(CSO_READ_BUFFER_SIZE, frameSize + (1u << indexShift));
	if (compressedReadEnd < compressedReadPos || compressedReadSize > capacity) {
		memset(outPtr, 0, GetBlockSize());
		return false;
	}
	const u32 compressedOffset = (blockNumber & ((1 << blockShift) - 1)) * GetBlockSize();

	const int plain = idx & 0x80000000;
	if (plain)
	{
		int readSize = (u32)fileLoader_->ReadAt(compressedReadPos + compressedOffset, 1, GetBlockSize(), outPtr, flags);
		if (readSize < GetBlockSize()) {
			memset(outPtr + readSize, 0, GetBlockSize() - readSize);
			return false;
		}
	}
	else if (zlibBufferFrame == frameNumber)
	{
		// We already have it.  Just apply the offset and copy.
		memcpy(outPtr, zlibBuffer + compressedOffset, GetBlockSize());
	}
	else
	{
		const u32 readSize = (u32)fileLoader_->ReadAt(compressedReadPos, 1, compressedReadSize, readBuffer, flags);

    z_stream *stream=ResetInflater();
    if(!stream)return false;
    z_stream &z=*stream;
    zlibBufferFrame=~0u;
		z.avail_in = readSize;
		z.next_out = frameSize == (u32)GetBlockSize() ? outPtr : zlibBuffer;
		z.avail_out = frameSize;
		z.next_in = readBuffer;

		int status;
    {Wii::CpuScope cpuInflate(Wii::CpuPhase::Inflate);status = inflate(&z, Z_FINISH);}
		if (status != Z_STREAM_END)
		{
			ERROR_LOG(LOADER, "block %d: inflate : %s[%d]\n", blockNumber, (z.msg) ? z.msg : "error", status);
			memset(outPtr, 0, GetBlockSize());
			return false;
		}
		if (z.total_out != frameSize)
		{
			ERROR_LOG(LOADER, "block %d: block size error %d != %d\n", blockNumber, (u32)z.total_out, frameSize);
			memset(outPtr, 0, GetBlockSize());
			return false;
		}

		if (frameSize != (u32)GetBlockSize())
		{
			zlibBufferFrame = frameNumber;
			memcpy(outPtr, zlibBuffer + compressedOffset, GetBlockSize());
		}
	}
	return true;
}

bool CISOFileBlockDevice::ReadBlocks(u32 minBlock, int count, u8 *outPtr) {
  Wii::CpuScope cpuLock(Wii::CpuPhase::Lock);
  std::lock_guard<std::recursive_mutex> guard(readMutex_);
  cpuLock.End();
  if(count==0)return minBlock<=numBlocks;
  if(!outPtr)return false;
	if (count < 0 || minBlock > numBlocks || static_cast<u32>(count) > numBlocks - minBlock)
		return false;
	if (count == 1) {
		return ReadBlockLocked(minBlock, outPtr, false);
	}
	if (minBlock >= numBlocks) {
		memset(outPtr, 0, GetBlockSize() * count);
		return false;
	}

	const u32 lastBlock = std::min(minBlock + count, numBlocks) - 1;
	const u32 missingBlocks = (lastBlock + 1 - minBlock) - count;
	if (lastBlock < minBlock + count) {
		memset(outPtr + GetBlockSize() * (count - missingBlocks), 0, GetBlockSize() * missingBlocks);
	}

	const u32 minFrameNumber = minBlock >> blockShift;
	const u32 lastFrameNumber = lastBlock >> blockShift;
	u32 afterLastIdx=0;
	if(!ReadIndex(lastFrameNumber+1,&afterLastIdx))return false;
	const u32 afterLastIndexPos = afterLastIdx & 0x7FFFFFFF;
	const u64 totalReadEnd = (u64)afterLastIndexPos << indexShift;

  z_stream *stream=ResetInflater();
  if(!stream)return false;
  z_stream &z=*stream;
  // A failure may overwrite zlibBuffer; do not leave a stale frame memo.
  zlibBufferFrame=~0u;

	u64 readBufferStart = 0;
	u64 readBufferEnd = 0;
	u32 block = minBlock;
	const u32 blocksPerFrame = 1 << blockShift;
	for (u32 frame = minFrameNumber; frame <= lastFrameNumber; ++frame) {
		u32 idx=0,nextIdx=0;
		if(!ReadIndex(frame,&idx)||!ReadIndex(frame+1,&nextIdx)) { return false; }
		const u32 indexPos = idx & 0x7FFFFFFF;
		const u32 nextIndexPos = nextIdx & 0x7FFFFFFF;

		const u64 frameReadPos = (u64)indexPos << indexShift;
		const u64 frameReadEnd = (u64)nextIndexPos << indexShift;
		if (frameReadEnd < frameReadPos || frameReadEnd > totalReadEnd || frameReadPos < readBufferStart && readBufferEnd > readBufferStart) {
			ERROR_LOG(LOADER, "Invalid/truncated CSO batch at block %u", block);
			return false;
		}
		const u64 frameReadSize64 = frameReadEnd - frameReadPos;
		if (frameReadSize64 > std::max<size_t>(CSO_READ_BUFFER_SIZE, frameSize + (1u << indexShift))) {
			ERROR_LOG(LOADER, "Invalid/truncated CSO batch at block %u", block);
			return false;
		}
		const u32 frameReadSize = (u32)frameReadSize64;
		const u32 frameBlockOffset = block & ((1 << blockShift) - 1);
		const u32 frameBlocks = std::min(lastBlock - block + 1, blocksPerFrame - frameBlockOffset);

		if (frameReadEnd > readBufferEnd) {
			const s64 maxNeeded = totalReadEnd - frameReadPos;
			const size_t chunkSize = (size_t)std::min(maxNeeded, (s64)std::max(frameReadSize, CSO_READ_BUFFER_SIZE));

			const u32 readSize = (u32)fileLoader_->ReadAt(frameReadPos, 1, chunkSize, readBuffer);
			if (readSize < chunkSize) {
				memset(readBuffer + readSize, 0, chunkSize - readSize);
			}

      if(readSize<frameReadEnd-frameReadPos)return false;
			readBufferStart = frameReadPos;
			readBufferEnd = frameReadPos + readSize;
		}

		u8 *rawBuffer = &readBuffer[frameReadPos - readBufferStart];
		const int plain = idx & 0x80000000;
		if (plain) {
      if(frameReadSize<uint64_t(frameBlockOffset+frameBlocks)*GetBlockSize())return false;
			memcpy(outPtr, rawBuffer + frameBlockOffset * GetBlockSize(), frameBlocks * GetBlockSize());
		} else {
      zlibBufferFrame=~0u;
			z.avail_in = frameReadSize;
			z.next_out = frameBlocks == blocksPerFrame ? outPtr : zlibBuffer;
			z.avail_out = frameSize;
			z.next_in = rawBuffer;

			int status;
    {Wii::CpuScope cpuInflate(Wii::CpuPhase::Inflate);status = inflate(&z, Z_FINISH);}
			if (status != Z_STREAM_END) {
				ERROR_LOG(LOADER, "Inflate frame %d: failed - %s[%d]\n", frame, (z.msg) ? z.msg : "error", status);
				memset(outPtr, 0, frameBlocks * GetBlockSize());
        return false;
			} else if (z.total_out != frameSize) {
				ERROR_LOG(LOADER, "Inflate frame %d: block size error %d != %d\n", frame, (u32)z.total_out, frameSize);
				memset(outPtr, 0, frameBlocks * GetBlockSize());
        return false;
			} else if (frameBlocks != blocksPerFrame) {
				memcpy(outPtr, zlibBuffer + frameBlockOffset * GetBlockSize(), frameBlocks * GetBlockSize());
				// In case we end up reusing it in a single read later.
				zlibBufferFrame = frame;
			}

			if(inflateReset(&z)!=Z_OK)return false;
		}

		block += frameBlocks;
		outPtr += frameBlocks * GetBlockSize();
	}

	return true;
}

NPDRMDemoBlockDevice::NPDRMDemoBlockDevice(FileLoader *fileLoader)
	: fileLoader_(fileLoader)
{
	std::lock_guard<std::mutex> guard(mutex_);
	MAC_KEY mkey;
	CIPHER_KEY ckey;
	u8 np_header[256]{};
	u32 tableOffset, tableSize;
	u32 lbaStart, lbaEnd;

	u32_le psar;
	if (fileLoader_->FileSize()<0x28 || fileLoader_->ReadAt(0x24,1,4,&psar)!=4)return;
	psarOffset=psar;
	if (uint64_t(psarOffset)+sizeof(np_header)>uint64_t(fileLoader_->FileSize()))return;
	size_t readSize = fileLoader_->ReadAt(psarOffset, 1, 256, &np_header);
	if(readSize!=256){
		ERROR_LOG(LOADER, "Invalid NPUMDIMG header!");
		return;
	}
	if(memcmp(np_header,"NPUMDIMG",8)!=0)return;

	kirk_init();

	// getkey
	sceDrmBBMacInit(&mkey, 3);
	sceDrmBBMacUpdate(&mkey, np_header, 0xc0);
	bbmac_getkey(&mkey, np_header+0xc0, vkey);

	// decrypt NP header
	memcpy(hkey, np_header+0xa0, 0x10);
	sceDrmBBCipherInit(&ckey, 1, 2, hkey, vkey, 0);
	sceDrmBBCipherUpdate(&ckey, np_header+0x40, 0x60);
	sceDrmBBCipherFinal(&ckey);

	auto header32=[&](unsigned at) {u32_le value;memcpy(&value,np_header+at,4);return u32(value);};
	lbaStart = header32(0x54); // LBA start
	lbaEnd   = header32(0x64); // LBA end
	lbaSize  = (lbaEnd-lbaStart+1);     // LBA size of ISO
	blockLBAs = header32(0x0c); // block size in LBA
	if(lbaEnd<lbaStart || uint64_t(lbaEnd)-lbaStart+1>0x100000 || blockLBAs<=0 || blockLBAs>512)return;
	blockSize = blockLBAs*2048;
	numBlocks = (lbaSize+blockLBAs-1)/blockLBAs; // total blocks;

	blockBuf = new(std::nothrow) u8[blockSize];
	tempBuf = new(std::nothrow) u8[blockSize];
	if(!blockBuf || !tempBuf)return;

	tableOffset = header32(0x6c); // table offset

	tableSize = numBlocks*32;
	if(uint64_t(psarOffset)+tableOffset+tableSize>uint64_t(fileLoader_->FileSize()))return;
	table = new(std::nothrow) table_info[numBlocks];
	if(!table)return;

	readSize = fileLoader_->ReadAt(uint64_t(psarOffset) + tableOffset, 1, tableSize, table);
	if(readSize!=tableSize){
		ERROR_LOG(LOADER, "Invalid NPUMDIMG table!");
		return;
	}

	u32 *p = (u32*)table;
	u32 i, k0, k1, k2, k3;
	for(i=0; i<numBlocks; i++){
		k0 = p[0]^p[1];
		k1 = p[1]^p[2];
		k2 = p[0]^p[3];
		k3 = p[2]^p[3];
		p[4] ^= k3;
		p[5] ^= k1;
		p[6] ^= k2;
		p[7] ^= k0;
		p += 8;
	}

	valid_=true;
	currentBlock = -1;

}

NPDRMDemoBlockDevice::~NPDRMDemoBlockDevice()
{
	std::lock_guard<std::mutex> guard(mutex_);
	delete [] table;
	delete [] tempBuf;
	delete [] blockBuf;
}

int lzrc_decompress(void *out, int out_len, void *in, int in_len);

bool NPDRMDemoBlockDevice::ReadBlock(int blockNumber, u8 *outPtr, bool uncached)
{
	FileLoader::Flags flags = uncached ? FileLoader::Flags::HINT_UNCACHED : FileLoader::Flags::NONE;
	std::lock_guard<std::mutex> guard(mutex_);
	CIPHER_KEY ckey;
	int block, lba, lzsize;
	size_t readSize;
	u8 *readBuf;

	if(!valid_ || !outPtr || blockNumber<0 || u32(blockNumber)>=lbaSize)return false;
	lba = blockNumber-currentBlock;
	if(currentBlock>=0 && lba>=0 && lba<blockLBAs){
		memcpy(outPtr, blockBuf+lba*2048, 2048);
		return true;
	}

	block = blockNumber/blockLBAs;
	lba = blockNumber%blockLBAs;
	currentBlock = -1;

	if(table[block].unk_1c!=0){
		if((u32)block==(numBlocks-1)) {
			memset(outPtr,0,2048);return true; // Sparse fake_np tail.
		}
		else
			return false;
	}

	const int storedSize=table[block].size;
	const uint64_t offset=uint64_t(psarOffset)+u32(table[block].offset);
	if(storedSize<=0 || storedSize>blockSize || offset+unsigned(storedSize)>uint64_t(fileLoader_->FileSize()) ||
		((table[block].flag&4)==0 && (storedSize&15)))return false;
	if(table[block].size<blockSize)
		readBuf = tempBuf;
	else
		readBuf = blockBuf;

	readSize = fileLoader_->ReadAt(offset, 1, storedSize, readBuf, flags);
	if(readSize != (size_t)table[block].size){
		return false;
	}

	if((table[block].flag&1)==0){
		// skip mac check
	}

	if((table[block].flag&4)==0){
		sceDrmBBCipherInit(&ckey, 1, 2, hkey, vkey, table[block].offset>>4);
		sceDrmBBCipherUpdate(&ckey, readBuf, table[block].size);
		sceDrmBBCipherFinal(&ckey);
	}

	if(table[block].size<blockSize){
		lzsize = lzrc_decompress(blockBuf, blockSize, readBuf, storedSize);
		if(lzsize!=blockSize){
			ERROR_LOG(LOADER, "LZRC decompress error! lzsize=%d\n", lzsize);
			return false;
		}
	}

	currentBlock=block*blockLBAs;
	memcpy(outPtr, blockBuf+lba*2048, 2048);

	return true;
}
