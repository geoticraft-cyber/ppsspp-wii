// SPDX-License-Identifier: GPL-2.0-or-later
#include "Wii/FileLogging.h"
#include "Wii/Diagnostics.h"
#include "Wii/AllocationDiagnostics.h"
#include <cstdio>
namespace Wii {
namespace {
FILE *normal=nullptr,*console=nullptr,*errors=nullptr;
FILE *originalOut=nullptr,*originalErr=nullptr;
bool rotated=false;
}
void CloseFileLogging() {
  SetDiagnosticLevel(0);
  SetAllocationLogging(false);
  DetachDiagnostics();
  CloseAllocationLog();
  // Keep the original runtime streams alive. Swapping the newlib FILE pointers
  // avoids reopening/closing descriptors 1/2 or relying on a FAT /dev/null file.
  if(console){fflush(console);stdout=originalOut;fclose(console);console=nullptr;}
  if(errors){fflush(errors);stderr=originalErr;fclose(errors);errors=nullptr;}
  if(normal){fclose(normal);normal=nullptr;}
}
bool SetFileLogging(const std::string &root,bool enabled,unsigned detail) {
  if(!enabled){CloseFileLogging();return true;}
  if(normal){SetDiagnosticLevel(detail);return true;}
  if(!rotated) {
    for(const char *name:{"ppsspp.log","errors.log","console.log"}) {
      const std::string path=root+name,previous=path+".previous";
      std::remove(previous.c_str());std::rename(path.c_str(),previous.c_str());
    }
    rotated=true;
  }
  normal=fopen((root+"ppsspp.log").c_str(),"a");
  console=fopen((root+"console.log").c_str(),"a");
  errors=fopen((root+"errors.log").c_str(),"a");
  if(!normal||!console||!errors) {
    if(normal)fclose(normal);if(console)fclose(console);if(errors)fclose(errors);
    normal=console=errors=nullptr;SetDiagnosticLevel(0);SetAllocationLogging(false);return false;
  }
  setvbuf(normal,nullptr,_IOFBF,64*1024);
  setvbuf(errors,nullptr,_IONBF,0);
  originalOut=stdout;originalErr=stderr;fflush(stdout);fflush(stderr);
  stdout=console;stderr=errors;
  AttachDiagnostics(normal,errors);SetDiagnosticLevel(detail);
  SetAllocationLogging(true);OpenAllocationLog((root+"allocation-failures.log").c_str());
  return true;
}
void FlushFileLogging(){FlushDiagnostics();if(console)fflush(console);}
}
