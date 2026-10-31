// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <cstdio>
namespace Wii {
enum class TraceKind : unsigned {
  Instruction,SyscallEnter,SyscallExit,ThreadSwitch,GECommand,IOBegin,IOEnd,
  Frame,Session,Count
};
// 0 off, 1 subsystem events, 2 sampled instructions, 3 full interpreter trace.
void SetDiagnosticLevel(unsigned level);
unsigned DiagnosticLevel();
void AttachDiagnostics(FILE *normal,FILE *errors);
void DetachDiagnostics();
void DiagnosticMessage(unsigned level,const char *header,const char *message,uint32_t pc,unsigned phase);
void DiagnosticText(const char *kind,const char *text);
void TraceEvent(TraceKind kind,uint32_t pc,uint32_t a=0,uint32_t b=0,uint32_t c=0);
void TraceInstruction(uint32_t pc,uint32_t opcode,uint32_t sp,uint32_t ra);
void DumpDiagnosticTrace(const char *reason,bool toErrors=false);
void DiagnosticCPU(uint32_t pc,const uint32_t *r,uint32_t hi,uint32_t lo,
  uint32_t fcr31,int downcount,bool delaySlot,uint32_t nextPC);
void FlushDiagnostics();
unsigned DiagnosticIOFailures();
}
