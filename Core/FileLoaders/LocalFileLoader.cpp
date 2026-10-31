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

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <limits>
#include <algorithm>
#include <cstdlib>
#include <malloc.h>
#include "ppsspp_config.h"
#include "util/text/utf8.h"
#include "file/file_util.h"
#include "Common/FileUtil.h"
#include "base/timeutil.h"
#include "Core/FileLoaders/LocalFileLoader.h"
#if PPSSPP_PLATFORM(WII)
#include "Wii/Runtime.h"
#include "Wii/IOClock.h"
#include "Wii/Diagnostics.h"
#include "Wii/Settings.h"
#endif

#ifdef _WIN32
#include "Common/CommonWindows.h"
#else
#include <fcntl.h>
#endif

LocalFileLoader::LocalFileLoader(const std::string &filename)
	: filesize_(0), filename_(filename) {

#ifndef _WIN32

	fd_ = open(filename.c_str(), O_RDONLY
#if !PPSSPP_PLATFORM(WII)
		| O_CLOEXEC
#endif
	);
	if (fd_ == -1) {
		ERROR_LOG(LOADER, "open(%s) failed: %s", filename.c_str(), strerror(errno));
		return;
	}
#if PPSSPP_PLATFORM(ANDROID) || (defined(_FILE_OFFSET_BITS) && _FILE_OFFSET_BITS < 64)
	off64_t off = lseek64(fd_, 0, SEEK_END);
	filesize_ = off;
	lseek64(fd_, 0, SEEK_SET);
#else
	off_t off = lseek(fd_, 0, SEEK_END);
	if (off < 0) {
		ERROR_LOG(LOADER, "Cannot seek %s: %s", filename.c_str(), strerror(errno));
		close(fd_); fd_ = -1; return;
	}
	filesize_ = off;
	lseek(fd_, 0, SEEK_SET);
#endif

#if PPSSPP_PLATFORM(WII)
  std::string lower=filename;for(auto &ch:lower)if(ch>='A'&&ch<='Z')ch+=32;
  usbDevice_=lower.compare(0,5,"usb:/")==0;
  if(usbDevice_)cache_.capacity=Wii::GetSettings().lowMemory?64*1024:Wii::MaxUSBTransfer;
  retainedCount_=Wii::GetSettings().lowMemory?0:3;
  for(auto &window:retained_)window.capacity=cache_.capacity;
  discCache_=lower.size()>=4&&(lower.substr(lower.size()-4)==".iso"||lower.substr(lower.size()-4)==".cso");
#endif
#else // !_WIN32

	const DWORD access = GENERIC_READ, share = FILE_SHARE_READ, mode = OPEN_EXISTING, flags = FILE_ATTRIBUTE_NORMAL;
#if PPSSPP_PLATFORM(UWP)
	handle_ = CreateFile2(ConvertUTF8ToWString(filename).c_str(), access, share, mode, nullptr);
#else
	handle_ = CreateFile(ConvertUTF8ToWString(filename).c_str(), access, share, nullptr, mode, flags, nullptr);
#endif
	if (handle_ == INVALID_HANDLE_VALUE) {
		return;
	}
	LARGE_INTEGER end_offset;
	const LARGE_INTEGER zero = { 0 };
	if(SetFilePointerEx(handle_, zero, &end_offset, FILE_END) == 0) {
		return;
	}
	filesize_ = end_offset.QuadPart;
	SetFilePointerEx(handle_, zero, nullptr, FILE_BEGIN);

#endif // !_WIN32

}

LocalFileLoader::~LocalFileLoader() {
#if PPSSPP_PLATFORM(WII)
  if(discCache_) {
    const double rate=ioTime_>0.0?(double(physicalBytes_)/1048576.0/ioTime_):0.0;
    INFO_LOG(BOOT,"DISC I/O %s: reads=%llu bytes=%llu cache-hits=%llu io-time=%.3fs read-rate=%.2fMiB/s read-ahead=%u max-read=%u windows=%u",
      filename_.c_str(),(unsigned long long)physicalReads_,(unsigned long long)physicalBytes_,
      (unsigned long long)cacheHits_,ioTime_,rate,unsigned(cache_.capacity),unsigned(maxReadRequest_),1+retainedCount_);
  }
  free(cache_.data);
  for(auto &window:retained_)free(window.data);
#endif
#ifndef _WIN32
	if (fd_ != -1) {
		close(fd_);
	}
#else
	if (handle_ != INVALID_HANDLE_VALUE) {
		CloseHandle(handle_);
	}
#endif
}

bool LocalFileLoader::Exists() {
	// If we couldn't open it for reading, we say it does not exist.
#ifndef _WIN32
	if (fd_ != -1 || IsDirectory()) {
#else
	if (handle_ != INVALID_HANDLE_VALUE || IsDirectory()) {
#endif
		FileInfo info;
		return getFileInfo(filename_.c_str(), &info);
	}
	return false;
}

bool LocalFileLoader::IsDirectory() {
	FileInfo info;
	if (getFileInfo(filename_.c_str(), &info)) {
		return info.isDirectory;
	}
	return false;
}

s64 LocalFileLoader::FileSize() {
	return filesize_;
}

std::string LocalFileLoader::Path() const {
	return filename_;
}

size_t LocalFileLoader::ReadAt(s64 absolutePos, size_t bytes, size_t count, void *data, Flags flags) {
#if PPSSPP_PLATFORM(ANDROID)
	// pread64 doesn't appear to actually be 64-bit safe, though such ISOs are uncommon.  See #10862.
	if (absolutePos <= 0x7FFFFFFF) {
#if defined(_FILE_OFFSET_BITS) && _FILE_OFFSET_BITS < 64
		return pread64(fd_, data, bytes * count, absolutePos) / bytes;
#else
		return pread(fd_, data, bytes * count, absolutePos) / bytes;
#endif
	} else {
		// Since pread64 doesn't change the file offset, it should be safe to avoid the lock in the common case.
		std::lock_guard<std::mutex> guard(readLock_);
		lseek64(fd_, absolutePos, SEEK_SET);
		return read(fd_, data, bytes * count) / bytes;
	}
#elif PPSSPP_PLATFORM(WII)
	if (absolutePos < 0 || absolutePos > std::numeric_limits<off_t>::max() ||
		bytes == 0 || count == 0 || count > std::numeric_limits<size_t>::max() / bytes)
		return 0;
  Wii::CpuScope cpuIO(Wii::CpuPhase::IO);
  Wii::CpuScope cpuLock(Wii::CpuPhase::Lock);
  std::lock_guard<std::mutex> guard(readLock_);
  cpuLock.End();
  const size_t requested=bytes*count;
  Wii::CpuProfileCount(7,requested);
  auto readFully=[&](uint64_t at,uint8_t *destination,size_t length) {
    Wii::RuntimeProgress(uint32_t(at),5);
    Wii::TraceEvent(Wii::TraceKind::IOBegin,uint32_t(at),uint32_t(at>>32),uint32_t(length),0);
    if(physicalOffset_!=at && lseek(fd_,static_cast<off_t>(at),SEEK_SET)==static_cast<off_t>(-1)) {
      physicalOffset_=~uint64_t(0);return size_t(0);
    }
    physicalOffset_=at;
    size_t total=0;
    while(total<length) {
      ++physicalReads_;
      Wii::RuntimeProgress(uint32_t(at+total),5);
      const uint64_t began=Wii::IOClockMicros();
      const size_t chunk=usbDevice_?std::min(length-total,Wii::MaxUSBTransfer):length-total;
      maxReadRequest_=std::max(maxReadRequest_,chunk);
      ssize_t got=read(fd_,destination+total,chunk);
      ioTime_+=double(Wii::IOClockMicros()-began)/1000000.0;
      if(got<0&&errno==EINTR)continue;
      if(got<0) {physicalOffset_=~uint64_t(0);ERROR_LOG(LOADER,"Read failed at %llu in %s: %s",(unsigned long long)(at+total),filename_.c_str(),strerror(errno));break;}
      if(got==0) {if(at+total<filesize_)ERROR_LOG(LOADER,"Unexpected EOF at %llu in %s",(unsigned long long)(at+total),filename_.c_str());break;}
      Wii::CpuProfileCount(9,size_t(got));
      total+=size_t(got);physicalBytes_+=size_t(got);physicalOffset_=at+total;
    }
    Wii::TraceEvent(Wii::TraceKind::IOEnd,uint32_t(at),uint32_t(at>>32),uint32_t(total),uint32_t(length));
    return total;
  };
  if(uint64_t(absolutePos)>=filesize_)return 0;
  size_t length=std::min<uint64_t>(requested,filesize_-uint64_t(absolutePos));
  if(flags==Flags::HINT_UNCACHED)readAhead_.Reset();
  const size_t window=(discCache_&&flags!=Flags::HINT_UNCACHED)?
    readAhead_.Observe(uint64_t(absolutePos),length,cache_.capacity):0;
  if(discCache_&&flags!=Flags::HINT_UNCACHED&&length<=cache_.capacity) {
    if(!cache_.data)cache_.data=static_cast<uint8_t *>(memalign(32,cache_.capacity));
    if(cache_.data) {
      size_t copied=0;
      while(copied<length) {
        const uint64_t at=uint64_t(absolutePos)+copied;
        if(!cache_.Contains(at,1)) {
          bool found=false;
          for(unsigned slot=0;slot<retainedCount_;++slot) {
            if(retained_[slot].Contains(at,1)) {
              std::swap(cache_,retained_[slot]);found=true;break;
            }
          }
          if(!found&&retainedCount_) {
            std::swap(cache_,retained_[nextVictim_]);
            nextVictim_=(nextVictim_+1)%retainedCount_;
          }
          if(!cache_.data)cache_.data=static_cast<uint8_t *>(memalign(32,cache_.capacity));
          if(!cache_.data) {
            copied+=readFully(at,static_cast<uint8_t *>(data)+copied,length-copied);
            break;
          }
        }
        if(cache_.Contains(at,1)) {++cacheHits_;Wii::CpuProfileCount(8,std::min(length-copied,cache_.valid-size_t(at-cache_.begin)));}
        else {
          cache_.begin=at&~uint64_t(2047);
          // Include the prefix introduced by sector alignment; large requests
          // may span two fills, but a random tiny read no longer fills the cap.
          const size_t prefix=size_t(at-cache_.begin);
          const size_t fill=std::min(cache_.capacity,std::max(window,prefix+std::min(length-copied,cache_.capacity-prefix)));
          cache_.valid=readFully(cache_.begin,cache_.data,std::min<uint64_t>(fill,filesize_-cache_.begin));
        }
        if(!cache_.Contains(at,1))break;
        const size_t available=cache_.valid-size_t(at-cache_.begin);
        const size_t n=std::min(length-copied,available);
        cache_.Copy(at,n,static_cast<uint8_t *>(data)+copied);copied+=n;
      }
      return copied/bytes;
    }
  }
  return readFully(absolutePos,static_cast<uint8_t *>(data),length)/bytes;
#elif defined(__wiiu__)
	std::lock_guard<std::mutex> guard(readLock_);
	lseek(fd_, absolutePos, SEEK_SET);
	return read(fd_, data, bytes * count) / bytes;
#elif !defined(_WIN32)
#if defined(_FILE_OFFSET_BITS) && _FILE_OFFSET_BITS < 64
	return pread64(fd_, data, bytes * count, absolutePos) / bytes;
#else
	return pread(fd_, data, bytes * count, absolutePos) / bytes;
#endif
#else
	DWORD read = -1;
	OVERLAPPED offset = { 0 };
	offset.Offset = (DWORD)(absolutePos & 0xffffffff);
	offset.OffsetHigh = (DWORD)((absolutePos & 0xffffffff00000000) >> 32);
	auto result = ReadFile(handle_, data, (DWORD)(bytes * count), &read, &offset);
	return result == TRUE ? (size_t)read / bytes : -1;
#endif
}
