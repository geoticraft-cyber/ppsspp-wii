// SPDX-License-Identifier: GPL-2.0-or-later
#include "Wii/Diagnostics.h"
#include "Wii/SyscallLog.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>
static bool failFlush=false;
extern "C" int __real_fflush(FILE *);
extern "C" int __wrap_fflush(FILE *f) {return failFlush?EOF:__real_fflush(f);}
static std::string Read(FILE *file) {
  fflush(file);rewind(file);std::string result;char text[4096];
  while(size_t n=fread(text,1,sizeof(text),file))result.append(text,n);
  return result;
}
extern "C" int __lsan_is_turned_off() {return 1;}
int main() {
  Wii::SyscallLogSet described;
  // The previous direct table repeatedly logged these colliding encodings.
  for(unsigned repeat=0;repeat<10000;++repeat)for(uint32_t op:{0x003c030cu,0x004c030cu,0x005c030cu})
    assert(described.First(op)==(repeat==0));
  for(unsigned i=1;i<300;++i)described.First((i<<6)|12);
  assert(!described.First(0x0ffffffc));described.Clear();assert(described.First(0x0ffffffc));
  FILE *normal=fopen("diagnostics-test.log","w+"),*errors=fopen("diagnostics-test-errors.log","w+");assert(normal&&errors);
  Wii::AttachDiagnostics(normal,errors);Wii::SetDiagnosticLevel(1);
  for(unsigned i=0;i<1100;++i)Wii::TraceEvent(Wii::TraceKind::IOBegin,0x08000000+i*4,i);
  Wii::DiagnosticMessage(4,"source.cpp:42 I[CPU]","normal message",0x08800000,1);
  Wii::DiagnosticMessage(2,"source.cpp:43 E[CPU]","test error",0x08800004,1);
  auto out=Read(normal),err=Read(errors);
  assert(out.find("source.cpp:42 I[CPU]")!=std::string::npos);
  assert(out.find("progress-pc=08800004")!=std::string::npos);
  assert(err.find("test error")!=std::string::npos&&err.find("normal message")==std::string::npos);
  assert(out.find("retained=1024 overwritten=76")!=std::string::npos);
  assert(out.find("seq=1100")!=std::string::npos);
  fseek(normal,0,SEEK_END);fseek(errors,0,SEEK_END);
  Wii::SetDiagnosticLevel(3);
  for(unsigned i=0;i<2500;++i)Wii::TraceInstruction(0x08800000+i*4,0x24020001,0x09ffff00,0x08800040);
  uint32_t r[32]={};r[31]=0x88776655;
  Wii::DiagnosticCPU(0x08800000,r,1,2,3,-1,true,0x08800040);
  Wii::DumpDiagnosticTrace("watchdog-no-progress");
  out=Read(normal);
  // Every emitted instruction event must be present despite exceeding capacity.
  for(unsigned i=1101;i<=3600;++i)assert(out.find("trace seq="+std::to_string(i)+" ")!=std::string::npos);
  assert(out.find("r31=88776655")!=std::string::npos);
  assert(out.find("delay=1 downcount=-1")!=std::string::npos);
  fseek(normal,0,SEEK_END);fseek(errors,0,SEEK_END);
  // Concurrent writer/watchdog access uses one lock and never logs recursively.
  std::vector<std::thread> workers;
  for(unsigned i=0;i<3;++i)workers.emplace_back([i]() {for(unsigned n=0;n<500;++n)Wii::TraceEvent(Wii::TraceKind::IOBegin,n,i,n,0);});
  for(auto &worker:workers)worker.join();Wii::DumpDiagnosticTrace("concurrency");
  Wii::SetDiagnosticLevel(1);
  for(unsigned i=0;i<64;++i)Wii::TraceEvent(Wii::TraceKind::GECommand,i,1,0,0);
  Wii::DumpDiagnosticTrace("sampling");
  out=Read(normal);auto section=out.substr(out.rfind("trace-begin reason=sampling"));
  auto first=section.find("kind=ge-command");assert(first!=std::string::npos);
  assert(section.find("kind=ge-command",first+1)==std::string::npos);
  fseek(normal,0,SEEK_END);
  Wii::SetDiagnosticLevel(0);Wii::TraceInstruction(0,0,0,0);Wii::SetDiagnosticLevel(99);assert(Wii::DiagnosticLevel()==1);
  failFlush=true;Wii::FlushDiagnostics();assert(Wii::DiagnosticIOFailures()>0);failFlush=false;
  for(unsigned i=0;i<64;++i) {
    Wii::TraceEvent(Wii::TraceKind::SyscallEnter,i,0,0,0);
    Wii::TraceEvent(Wii::TraceKind::SyscallExit,i,0,0,0);
  }
  Wii::DumpDiagnosticTrace("syscall-sampling");
  out=Read(normal);section=out.substr(out.rfind("trace-begin reason=syscall-sampling"));
  for(const char *kind:{"kind=syscall-enter","kind=syscall-exit"}) {
    unsigned n=0;size_t offset=0;
    while((offset=section.find(kind,offset))!=std::string::npos){++n;++offset;}
    assert(n==4);
  }
  Wii::DumpDiagnosticTrace("heartbeat");
  Wii::DumpDiagnosticTrace("heartbeat");
  out=Read(normal);section=out.substr(out.rfind("trace-begin reason=heartbeat"));
  assert(section.find("trace seq=")==std::string::npos);
  fseek(normal,0,SEEK_END);
  Wii::DiagnosticMessage(2,"test","error after incremental dump",0,0);
  out=Read(normal);section=out.substr(out.rfind("trace-begin reason=error"));
  assert(section.find("trace seq=")==std::string::npos);
  err=Read(errors);assert(err.find("trace seq=")!=std::string::npos);
  fseek(normal,0,SEEK_END);const long beforeRepeated=ftell(normal);
  for(unsigned i=0;i<1176;++i)Wii::DiagnosticMessage(2,"probe","repeated error",0,0);
  fflush(normal);assert(ftell(normal)-beforeRepeated<400000);
  Wii::DetachDiagnostics();fclose(normal);fclose(errors);
  std::remove("diagnostics-test.log");std::remove("diagnostics-test-errors.log");
  puts("pass: headers/severity, dual error logs, ring wrap accounting, complete full-mode events, registers, concurrent trace writers and detach");
}
