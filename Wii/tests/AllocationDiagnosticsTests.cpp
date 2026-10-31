// SPDX-License-Identifier: GPL-2.0-or-later
#include "Wii/AllocationDiagnostics.h"
#include <cassert>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <unistd.h>
static bool fail=false, interrupt=false, reenter=false, broken=false;
static char storage[128], trace[32768];static size_t traceSize=0;
extern "C" {
void *__real_malloc(size_t) {if(fail) {errno=ENOMEM;return nullptr;}return storage;}
void *__real_calloc(size_t,size_t) {return __real_malloc(1);}
void *__real_realloc(void *,size_t size) {return size?__real_malloc(size):nullptr;}
void *__real_memalign(size_t,size_t size) {return __real_malloc(size);}
void *__real_SYS_AllocArenaMem2Lo(unsigned,unsigned) {return fail?nullptr:storage;}
void *__wrap_malloc(size_t);void *__wrap_calloc(size_t,size_t);
void *__wrap_realloc(void *,size_t);void *__wrap_memalign(size_t,size_t);
void *__wrap_SYS_AllocArenaMem2Lo(unsigned,unsigned);
ssize_t __wrap_write(int,const void *p,size_t bytes) {
  if(reenter) {reenter=false;Wii::AllocationFailure("nested",9,0,0,0,0);}
  if(interrupt) {interrupt=false;errno=EINTR;return -1;}
  if(broken) {errno=EIO;return -1;}
  if(bytes>7)bytes=7;
  assert(traceSize+bytes<sizeof(trace));memcpy(trace+traceSize,p,bytes);traceSize+=bytes;trace[traceSize]=0;
  return bytes;
}
}
int main(int argc,char **argv) {
  assert(argc==2 && Wii::OpenAllocationLog(argv[1]));
  Wii::AllocationSnapshot(0x08801234,100,4096,0,8192);
  Wii::DiagnosticPhase("guest-run");
  assert(__wrap_malloc(40)==storage && __wrap_calloc(3,4)==storage);
  assert(__wrap_realloc(storage,80)==storage && __wrap_memalign(32,64)==storage);
  assert(__wrap_SYS_AllocArenaMem2Lo(32,32)==storage);
  assert(Wii::AllocationFailureCount()==0);
  fail=true;interrupt=true;
  assert(!__wrap_malloc(12345) && errno==ENOMEM);
  assert(strstr(trace,"kind=malloc requested=12345"));
  assert(strstr(trace,"phase=guest-run") && strstr(trace,"pc=0x8801234"));
  assert(strstr(trace,"last-heap-free=4096"));
  assert(!__wrap_calloc(123,456));
  assert(strstr(trace,"kind=calloc requested=56088 count=123 element-size=456"));
  assert(!__wrap_calloc(size_t(-1),2));assert(strstr(trace,"kind=calloc-overflow"));
  storage[0]=42;assert(!__wrap_realloc(storage,654321));assert(storage[0]==42);
  assert(strstr(trace,"kind=realloc requested=654321"));
  assert(!__wrap_memalign(4096,1048576));
  assert(strstr(trace,"kind=memalign requested=1048576 detail=0 alignment=4096"));
  assert(!__wrap_SYS_AllocArenaMem2Lo(33554432,32));assert(strstr(trace,"kind=mem2-arena-lo requested=33554432"));
  const unsigned before=Wii::AllocationFailureCount();
  assert(!__wrap_malloc(0) && !__wrap_calloc(0,20) && !__wrap_realloc(storage,0));
  assert(Wii::AllocationFailureCount()==before); // Null is valid for zero bytes.
  reenter=true;assert(!__wrap_malloc(20));
  assert(Wii::AllocationFailureCount()==before+2 && Wii::AllocationUnwrittenCount()==1);
  broken=true;errno=ENOSPC;Wii::AllocationFailure("write-test",1,0,0,0,0);
  assert(errno==ENOSPC && Wii::AllocationUnwrittenCount()==2);
  broken=false;assert(!__wrap_malloc(30));assert(strstr(trace,"unwritten=2"));
  Wii::CloseAllocationLog();
  puts("pass: host/arena failure sizes, overflow, zero-size rules, errno, realloc ownership, short writes, EINTR, recursion and write-loss counters");
}
