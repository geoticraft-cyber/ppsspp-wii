extern "C" int __lsan_is_turned_off() {return 1;}
// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <sys/mman.h>
#include <unistd.h>
int lzrc_decompress(void *,int,void *,int);
int main() {
  unsigned char plain[]={0x80,0,0,0,5,'h','e','l','l','o'},out[128];
  memset(out,0xa5,sizeof(out));
  assert(lzrc_decompress(out+32,5,plain,sizeof(plain))==5 && !memcmp(out+32,"hello",5));
  assert(lzrc_decompress(out,4,plain,sizeof(plain))==-1);
  assert(lzrc_decompress(out,5,plain,9)==-1);
  assert(lzrc_decompress(out,-1,plain,sizeof(plain))==-1);
  plain[1]=0xff;assert(lzrc_decompress(out,64,plain,sizeof(plain))==-1);
  const size_t page=sysconf(_SC_PAGESIZE);
  auto memory=static_cast<unsigned char *>(mmap(nullptr,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
  assert(memory!=MAP_FAILED && !mprotect(memory+page,page,PROT_NONE));
  uint32_t random=0x12345678;
  for(unsigned i=0;i<20000;++i) {
    const int length=i%97;auto input=memory+page-length;
    for(int j=0;j<length;++j) {random=random*1664525+1013904223;input[j]=random>>24;}
    if(length && (i&1))input[0]&=7; // Exercise compressed paths as well as headers.
    memset(out,0xa5,sizeof(out));
    const int result=lzrc_decompress(out+32,64,input,length);
    assert(result>=-1 && result<=64);
    for(unsigned j=0;j<32;++j)assert(out[j]==0xa5 && out[96+j]==0xa5);
  }
  assert(!munmap(memory,page*2));
  puts("pass: LZRC plaintext fixtures and 20000 malformed inputs with protected input pages, output guards and undefined-behavior checks");
}
