// SPDX-License-Identifier: GPL-2.0-or-later
#include "Wii/AllocationDiagnostics.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <malloc.h>
#include <initializer_list>
#include <fcntl.h>
#include <unistd.h>
// The host has no libogc arena. All four C heap symbols use the real allocator.
extern "C" void *__real_SYS_AllocArenaMem2Lo(unsigned,unsigned) {return nullptr;}
int main(int argc,char **argv) {
  assert(argc==2 && Wii::OpenAllocationLog(argv[1]));
  const unsigned before=Wii::AllocationFailureCount();
  char *old=static_cast<char *>(malloc(64));assert(old);memset(old,0x5a,64);
  volatile size_t impossible=size_t(-1);
  assert(!malloc(impossible));assert(!calloc(impossible,2));
  assert(!realloc(old,impossible));
  for(unsigned i=0;i<64;++i)assert(old[i]==0x5a);
  assert(!memalign(32,impossible));free(old);
  assert(Wii::AllocationFailureCount()==before+4 && Wii::AllocationUnwrittenCount()==0);
  Wii::CloseAllocationLog();
  const int fd=open(argv[1],O_RDONLY);assert(fd>=0);
  char text[8192]={};const ssize_t n=read(fd,text,sizeof(text)-1);assert(n>0);close(fd);
  for(const char *kind:{"kind=malloc", "kind=calloc-overflow", "kind=realloc", "kind=memalign"})assert(strstr(text,kind));
  puts("pass: actual host linker wrappers and real allocation failures; direct file output and failed realloc ownership preserved");
}
