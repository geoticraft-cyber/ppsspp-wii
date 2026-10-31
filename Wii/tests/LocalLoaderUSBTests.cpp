// SPDX-License-Identifier: GPL-2.0-or-later
#include "Core/FileLoaders/LocalFileLoader.h"
#include "Wii/Settings.h"
#include "Wii/TransferLimits.h"
#include "Common/Log.h"
#include "file/file_util.h"
#include <cassert>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>
static size_t largestRead,largestAllocation;
static unsigned reads,seeks;
static bool failAllocation=false;
static bool shortReads=false,interruptNext=false,failSeek=false,failRead=false;
static std::string report;
extern "C" ssize_t __real_read(int,void *,size_t);
extern "C" off_t __real_lseek(int,off_t,int);
extern "C" void *__real_memalign(size_t,size_t);
extern "C" ssize_t __wrap_read(int fd,void *out,size_t bytes) {
  assert(bytes<=Wii::MaxUSBTransfer);largestRead=std::max(largestRead,bytes);++reads;
  if(failRead){failRead=false;errno=EIO;return -1;}
  if(interruptNext){interruptNext=false;errno=EINTR;return -1;}
  if(reads==1)usleep(1000); // deterministic nonzero elapsed I/O in the report
  return __real_read(fd,out,shortReads?std::min(bytes,size_t(4093)):bytes);
}
extern "C" off_t __wrap_lseek(int fd,off_t offset,int whence) {
  ++seeks;if(failSeek){failSeek=false;errno=EIO;return -1;}
  return __real_lseek(fd,offset,whence);
}
extern "C" void *__wrap_memalign(size_t alignment,size_t bytes) {
  largestAllocation=std::max(largestAllocation,bytes);if(failAllocation){failAllocation=false;return nullptr;}return __real_memalign(alignment,bytes);
}
void GenericLog(LogTypes::LOG_LEVELS,LogTypes::LOG_TYPE,const char *,int,const char *format,...) {
  char text[1024];va_list args;va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);
  if(!strncmp(text,"DISC I/O ",9))report=text;
}
bool GenericLogEnabled(LogTypes::LOG_LEVELS,LogTypes::LOG_TYPE){return true;}
bool getFileInfo(const char *path,FileInfo *info) {
  struct stat s{};if(stat(path,&s))return false;info->exists=true;info->isDirectory=S_ISDIR(s.st_mode);info->size=s.st_size;return true;
}
namespace Wii {void RuntimeProgress(uint32_t,unsigned) {}}
int main() {
  char cwd[4096];assert(getcwd(cwd,sizeof(cwd)));char temp[]="loader-usb-test-XXXXXX";
  assert(mkdtemp(temp)&&!chdir(temp));assert(!mkdir("usb:",0700));
  std::vector<uint8_t> source(768*1024+37);for(size_t i=0;i<source.size();++i)source[i]=uint8_t(i*19+(i>>9));
  FILE *file=fopen("usb:/game.cso","wb");assert(file);assert(fwrite(source.data(),1,source.size(),file)==source.size());assert(!fclose(file));
  for(bool low:{false,true}) {
    Wii::GetSettings().lowMemory=low;largestRead=largestAllocation=reads=seeks=0;
    {
      LocalFileLoader loader("usb:/game.cso");assert(loader.Exists()&&!loader.IsDirectory());
      std::vector<uint8_t> out(400003,0xcd);
      assert(loader.ReadAt(0,1,16,out.data())==16);
      const unsigned beforeReads=reads;
      assert(loader.ReadAt(1,1,16,out.data())==16&&reads==beforeReads);
      const size_t edge=(low?64:128)*1024;
      assert(largestRead==8192); // random tiny read does not fill the cap
      // A far probe starts another small window, retaining boundary bytes.
      assert(loader.ReadAt(edge-8,1,32,out.data())==32);
      assert(!memcmp(out.data(),source.data()+edge-8,32)&&reads==beforeReads+1);
      assert(largestAllocation==edge);
      // Consecutive sector requests grow read-ahead to the existing cap.
      for(size_t at=256*1024;at<512*1024;at+=2048) {
        assert(loader.ReadAt(at,1,2048,out.data())==2048);
        assert(!memcmp(out.data(),source.data()+at,2048));
      }
      assert(largestRead==edge);
      interruptNext=true;shortReads=true;
      for(size_t position:{size_t(0),size_t(1),edge-10,edge,source.size()-100,source.size(),source.size()+1}) {
        for(size_t length:{size_t(1),size_t(33),size_t(65536),size_t(131072),size_t(400000)}) {
          std::fill(out.begin(),out.end(),0xcd);
          const size_t expected=position>=source.size()?0:std::min(length,source.size()-position);
          assert(loader.ReadAt(position,1,length,out.data())==expected);
          assert(!memcmp(out.data(),source.data()+std::min(position,source.size()),expected));
          for(size_t i=expected;i<out.size();++i)assert(out[i]==0xcd);
        }
      }
      shortReads=false;
      if(!low) {
        // Metadata/data interleaving must retain both windows, not thrash.
        assert(loader.ReadAt(0,1,32,out.data())==32);
        assert(loader.ReadAt(600*1024,1,32,out.data())==32);
        const unsigned before=reads;
        for(unsigned i=0;i<1000;++i) {
          const size_t pos=i&1?600*1024:0;
          assert(loader.ReadAt(pos,1,32,out.data())==32);
          assert(!memcmp(out.data(),source.data()+pos,32));
        }
        assert(reads==before);
        puts("pass: 1000 alternating disc probes served without physical reads after two warmups");
      }
      assert(loader.ReadAt(9,7,37,out.data(),FileLoader::Flags::HINT_UNCACHED)==37);
      assert(!memcmp(out.data(),source.data()+9,7*37));
      assert(!loader.ReadAt(-1,1,4,out.data()));
      assert(!loader.ReadAt(0,std::numeric_limits<size_t>::max(),2,out.data()));
      assert(!loader.ReadAt(0,0,2,out.data()));
      failSeek=true;assert(!loader.ReadAt(500001,1,32,out.data(),FileLoader::Flags::HINT_UNCACHED));
      assert(loader.ReadAt(500001,1,32,out.data(),FileLoader::Flags::HINT_UNCACHED)==32);
      assert(!memcmp(out.data(),source.data()+500001,32));
      failRead=true;assert(!loader.ReadAt(400001,1,32,out.data(),FileLoader::Flags::HINT_UNCACHED));
      assert(loader.ReadAt(400001,1,32,out.data(),FileLoader::Flags::HINT_UNCACHED)==32);
      assert(!memcmp(out.data(),source.data()+400001,32));
      assert(largestRead<=128*1024);
    }
    const std::string measuredReport=report;
    {
      LocalFileLoader loader("usb:/game.cso");uint8_t out[32];
      failAllocation=true;
      assert(loader.ReadAt(0,1,32,out)==32&&!memcmp(out,source.data(),32));
      assert(loader.ReadAt(0,1,32,out)==32);
      failAllocation=true;
      assert(loader.ReadAt(600*1024,1,32,out)==32&&!memcmp(out,source.data()+600*1024,32));
      // A low-memory loader may reuse its sole buffer without allocating.
      failAllocation=false;
    }
    report=measuredReport;
    assert(report.find(low?"read-ahead=65536":"read-ahead=131072")!=std::string::npos);
    assert(report.find("io-time=0.000s")==std::string::npos);
    assert(report.find("read-rate=0.00MiB/s")==std::string::npos);
  }
  assert(!remove("usb:/game.cso")&&!rmdir("usb:"));assert(!chdir(cwd)&&!rmdir(temp));
  puts("PASS: actual USB LocalFileLoader: 128 KiB physical limit, 64/128 KiB buffers, adaptive random/sequential read-ahead, large logical reads, short reads/EINTR/seek errors, EOF guards, item counts and live I/O timing");
}
