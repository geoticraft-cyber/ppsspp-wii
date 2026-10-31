// SPDX-License-Identifier: GPL-2.0-or-later
#include "Wii/AllocationDiagnostics.h"
#include <atomic>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

namespace {
std::atomic<unsigned> failures{0}, unwritten{0}, snapshot{0}, pc{0}, used{0}, available{0}, arena1{0}, arena2{0};
std::atomic<const char *> phase{"startup"};
std::atomic<int> output{STDERR_FILENO};
std::atomic<bool> enabled{true};
std::atomic_flag writing=ATOMIC_FLAG_INIT;
// Fixed stack formatting avoids printf, streams, malloc, and the main logger.
struct Line {
  char bytes[768];size_t size=0;
  void text(const char *s) {while(*s && size<sizeof(bytes)-1)bytes[size++]=*s++;}
  void number(uintptr_t n,unsigned base=10) {
    char digits[2*sizeof(uintptr_t)+24];unsigned count=0;
    do {digits[count++]="0123456789abcdef"[n%base];n/=base;}while(n);
    while(count && size<sizeof(bytes)-1)bytes[size++]=digits[--count];
  }
  void field(const char *label,uintptr_t value,unsigned base=10) {text(label);number(value,base);}
};
}
namespace Wii {
void AllocationSnapshot(uint32_t p,unsigned u,unsigned f,unsigned a1,unsigned a2) {
  ++snapshot;
  pc.store(p);used.store(u);available.store(f);arena1.store(a1);arena2.store(a2);++snapshot;
}
void DiagnosticPhase(const char *literal) {phase.store(literal);}
void SetAllocationLogging(bool value) {enabled.store(value);}
unsigned AllocationFailureCount() {return failures.load();}
unsigned AllocationUnwrittenCount() {return unwritten.load();}
bool OpenAllocationLog(const char *path) {
  const int fd=open(path,O_WRONLY|O_CREAT|O_APPEND,0666);
  if(fd<0)return false;
  const int previous=output.exchange(fd);
  if(previous!=STDERR_FILENO)close(previous);
  const char header[]="session-start allocation-trace-v1 (counters restart at zero)\n";
  size_t sent=0;
  while(sent<sizeof(header)-1) {
    const ssize_t n=write(fd,header+sent,sizeof(header)-1-sent);
    if(n<0 && errno==EINTR)continue;
    if(n<=0) {++unwritten;break;}
    sent+=size_t(n);
  }
  return true;
}
void CloseAllocationLog() {
  const int fd=output.exchange(STDERR_FILENO);
  if(fd!=STDERR_FILENO) {fsync(fd);close(fd);}
}
void AllocationFailure(const char *kind,size_t requested,size_t detail,size_t alignment,
  uintptr_t caller,int error,uintptr_t address,unsigned guestFree,unsigned guestLargest) {
  const int saved=errno;
  const unsigned sequence=++failures;
  if(!enabled.load()){errno=saved;return;}
  // Filesystem writes may themselves allocate. Never recurse into logging or
  // wait on an interrupted writer. Account for every suppressed event explicitly.
  if(writing.test_and_set()) {++unwritten;errno=saved;return;}
  Line line;
  line.field("failure sequence=",sequence);line.text(" kind=");line.text(kind);
  line.field(" requested=",requested);
  const bool callocKind=kind[0]=='c' && kind[1]=='a';
  line.field(callocKind?" count=":" detail=",detail);
  line.field(callocKind?" element-size=":" alignment=",alignment);line.field(" caller=0x",caller,16);
  line.field(" address=0x",address,16);line.field(" errno=",unsigned(error));
  line.field(" guest-free=",guestFree);line.field(" guest-largest=",guestLargest);
  line.text(" phase=");line.text(phase.load());
  line.field(" snapshot-start=",snapshot.load());line.field(" pc=0x",pc.load(),16);
  line.field(" last-heap-used=",used.load());line.field(" last-heap-free=",available.load());
  line.text(" host-largest=unknown");
  line.field(" last-arena1=",arena1.load());line.field(" last-arena2=",arena2.load());
  line.field(" snapshot-end=",snapshot.load());
  line.field(" unwritten=",unwritten.load());line.text("\n");
  size_t sent=0;
  while(sent<line.size) {
    const ssize_t n=write(output.load(),line.bytes+sent,line.size-sent);
    if(n<0 && errno==EINTR)continue;
    if(n<=0) {++unwritten;break;}
    sent+=size_t(n);
  }
  writing.clear();errno=saved;
}
}

extern "C" {
void *__real_malloc(size_t);
void *__real_calloc(size_t,size_t);
void *__real_realloc(void *,size_t);
void *__real_memalign(size_t,size_t);
void *__real_SYS_AllocArenaMem2Lo(unsigned,unsigned);
void *__wrap_malloc(size_t bytes) {
  void *p=__real_malloc(bytes);
  if(!p && bytes)Wii::AllocationFailure("malloc",bytes,0,0,reinterpret_cast<uintptr_t>(__builtin_return_address(0)),errno);
  return p;
}
void *__wrap_calloc(size_t count,size_t bytes) {
  void *p=__real_calloc(count,bytes);
  if(!p && count && bytes) {
    const bool overflow=bytes>size_t(-1)/count;
    Wii::AllocationFailure(overflow?"calloc-overflow":"calloc",overflow?size_t(-1):count*bytes,
      count,bytes,reinterpret_cast<uintptr_t>(__builtin_return_address(0)),errno);
  }
  return p;
}
void *__wrap_realloc(void *old,size_t bytes) {
  void *p=__real_realloc(old,bytes);
  if(!p && bytes)Wii::AllocationFailure("realloc",bytes,0,0,reinterpret_cast<uintptr_t>(__builtin_return_address(0)),errno,reinterpret_cast<uintptr_t>(old));
  return p;
}
void *__wrap_memalign(size_t alignment,size_t bytes) {
  void *p=__real_memalign(alignment,bytes);
  if(!p && bytes)Wii::AllocationFailure("memalign",bytes,0,alignment,reinterpret_cast<uintptr_t>(__builtin_return_address(0)),errno);
  return p;
}
void *__wrap_SYS_AllocArenaMem2Lo(unsigned bytes,unsigned alignment) {
  void *p=__real_SYS_AllocArenaMem2Lo(bytes,alignment);
  if(!p && bytes)Wii::AllocationFailure("mem2-arena-lo",bytes,0,alignment,reinterpret_cast<uintptr_t>(__builtin_return_address(0)),0);
  return p;
}
}
