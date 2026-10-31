// SPDX-License-Identifier: GPL-2.0-or-later
#include "Wii/Diagnostics.h"
#include "Wii/IOClock.h"
#include <atomic>
#include <mutex>
#include <cstring>
namespace Wii {
namespace {
struct Event {uint64_t sequence,time;TraceKind kind;uint32_t pc,a,b,c;};
constexpr unsigned capacity=1024;
Event ring[capacity];
unsigned head=0,count=0;
uint64_t sequence=0,overwritten=0,logSequence=0,lastFlush=0,samples=0;
FILE *normalFile=nullptr,*errorFile=nullptr;
std::mutex lock;
std::atomic<unsigned> level{1},ioFailures{0};
std::atomic<bool> attached{false};
uint64_t normalSequence=0,errorSequence=0;
uint32_t geSamples=0,syscallSamples=0,syscallDepth=0;
bool traceSyscall=false;
const char *Name(TraceKind kind) {
  static const char *names[]={"instruction","syscall-enter","syscall-exit","thread-switch","ge-command","io-begin","io-end","frame","session"};
  return unsigned(kind)<unsigned(TraceKind::Count)?names[unsigned(kind)]:"unknown";
}
void Drain(FILE *file,const char *reason) {
  const bool incremental=std::strcmp(reason,"heartbeat")==0||std::strcmp(reason,"error")==0;
  if(!file)return;
  fprintf(file,"trace-begin reason=%s retained=%u overwritten=%llu level=%u fields=pc,a,b,c(hex)\n",reason,count,(unsigned long long)overwritten,level.load());
  uint64_t &delivered=file==normalFile?normalSequence:errorSequence;
  for(unsigned i=0;i<count;++i) {
    const auto &e=ring[(head+capacity-count+i)%capacity];
    if(incremental&&e.sequence<=delivered)continue;
    fprintf(file,"trace seq=%llu us=%llu kind=%s pc=%08x a=%08x b=%08x c=%08x\n",
      (unsigned long long)e.sequence,(unsigned long long)e.time,Name(e.kind),e.pc,e.a,e.b,e.c);
  }
  fputs("trace-end\n",file);
  delivered=sequence;
}
void FlushLocked() {
  if(normalFile && fflush(normalFile))++ioFailures;
  if(errorFile&&errorFile!=normalFile&&fflush(errorFile))++ioFailures;
  lastFlush=IOClockMicros();
}
}
void SetDiagnosticLevel(unsigned value) {level.store(value>3?1:value);}
unsigned DiagnosticLevel() {return level.load();}
unsigned DiagnosticIOFailures() {return ioFailures.load();}
void AttachDiagnostics(FILE *normal,FILE *errors) {
  std::lock_guard<std::mutex> guard(lock);
  normalFile=normal;errorFile=errors;head=count=0;normalSequence=errorSequence=0;syscallSamples=syscallDepth=0;traceSyscall=false;sequence=overwritten=logSequence=samples=0;lastFlush=IOClockMicros();ioFailures=0;
  attached.store(normalFile||errorFile);
  geSamples=0;
  if(normalFile)fputs("diagnostics-v3: monotonic-us, ordered events, recent history; mode 1 syscall pairs sampled 1/16; error records also go to errors.log\n",normalFile);
  FlushLocked();
}
void DetachDiagnostics() {
  std::lock_guard<std::mutex> guard(lock);
  attached.store(false);Drain(normalFile,"shutdown");FlushLocked();normalFile=errorFile=nullptr;
}
void DiagnosticMessage(unsigned severity,const char *header,const char *message,uint32_t pc,unsigned phase) {
  if(!attached.load())return;
  std::lock_guard<std::mutex> guard(lock);
  const uint64_t now=IOClockMicros();const auto seq=++logSequence;
  auto write=[&](FILE *file) {
    if(!file)return;
    fprintf(file,"log seq=%llu us=%llu level=%u progress-pc=%08x phase=%u %s %s",
      (unsigned long long)seq,(unsigned long long)now,severity,pc,phase,header?header:"",message?message:"");
    const size_t n=message?strlen(message):0;if(!n||message[n-1]!='\n')fputc('\n',file);
  };
  write(normalFile);
  // PPSSPP levels: notice=1, error=2, warning=3, info=4, debug=5.
  if(severity<=3&&errorFile!=normalFile)write(errorFile);
  if(severity==2) {Drain(normalFile,"error");if(errorFile!=normalFile)Drain(errorFile,"error");}
  if(severity<=3||now-lastFlush>=1000000)FlushLocked();
}
void DiagnosticText(const char *kind,const char *text) {
  if(!attached.load())return;
  std::lock_guard<std::mutex> guard(lock);
  if(normalFile)fprintf(normalFile,"event us=%llu kind=%s text=%s\n",(unsigned long long)IOClockMicros(),kind?kind:"unknown",text?text:"");
  FlushLocked();
}
void TraceEvent(TraceKind kind,uint32_t pc,uint32_t a,uint32_t b,uint32_t c) {
  const unsigned mode=level.load();if(!mode)return;
  // These producers execute on the guest CPU thread. Keep mode 3 exact;
  // normal modes must not timestamp/lock tens of millions of tiny blocks.
  if(mode<3) {
    // Mode 1 keeps paired syscall samples; mode 2 retains all syscall events.
    // Avoid a timer read and mutex twice per high-frequency guest audio call.
    if(mode==1) {
      if(kind==TraceKind::SyscallEnter) {
        if(!syscallDepth++)traceSyscall=(++syscallSamples&15)==0;
        if(!traceSyscall)return;
      } else if(kind==TraceKind::SyscallExit) {
        const bool keep=syscallDepth&&traceSyscall;
        if(syscallDepth)--syscallDepth;
        if(!keep)return;
      }
    }
    if(kind==TraceKind::GECommand && (++geSamples&63))return;
  }
  std::lock_guard<std::mutex> guard(lock);
  // Full mode streams before overwriting. Other modes retain bounded recent
  // history and explicitly account for overwritten entries when dumped.
  if(count==capacity&&mode==3&&normalFile) {Drain(normalFile,"full-trace-chunk");count=0;FlushLocked();}
  if(count==capacity)++overwritten;else ++count;
  ring[head]={++sequence,IOClockMicros(),kind,pc,a,b,c};head=(head+1)%capacity;
}
void TraceInstruction(uint32_t pc,uint32_t opcode,uint32_t sp,uint32_t ra) {
  const unsigned mode=level.load();
  if(mode<2)return;
  // This counter is owned by the guest CPU thread; only reset before starting it.
  if(mode==3||((++samples&255)==0))TraceEvent(TraceKind::Instruction,pc,opcode,sp,ra);
}
void DumpDiagnosticTrace(const char *reason,bool toErrors) {
  if(!attached.load())return;
  std::lock_guard<std::mutex> guard(lock);
  Drain(normalFile,reason?reason:"requested");if(toErrors&&errorFile!=normalFile)Drain(errorFile,reason?reason:"requested");FlushLocked();
}
void DiagnosticCPU(uint32_t pc,const uint32_t *r,uint32_t hi,uint32_t lo,uint32_t fcr31,int downcount,bool delaySlot,uint32_t nextPC) {
  if(!attached.load())return;
  std::lock_guard<std::mutex> guard(lock);
  if(!normalFile||!r)return;
  fprintf(normalFile,"cpu us=%llu pc=%08x next=%08x delay=%u downcount=%d hi=%08x lo=%08x fcr31=%08x\n",(unsigned long long)IOClockMicros(),pc,nextPC,unsigned(delaySlot),downcount,hi,lo,fcr31);
  for(unsigned base=0;base<32;base+=8) {
    fputs("gpr",normalFile);for(unsigned i=base;i<base+8;++i)fprintf(normalFile," r%u=%08x",i,r[i]);fputc('\n',normalFile);
  }
  FlushLocked();
}
void FlushDiagnostics() {
  if(!attached.load())return;std::lock_guard<std::mutex> guard(lock);FlushLocked();}
}
