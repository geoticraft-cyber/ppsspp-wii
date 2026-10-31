// SPDX-License-Identifier: GPL-2.0-or-later
// Frozen dispatch from the preceding optimized source. Test/benchmark only.
#pragma once
#include "Core/MIPS/MIPSInt.h"
#include "Core/MIPS/MIPSDis.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/Reporting.h"
#include "Wii/ScalarInterpreter.h"
#include "Wii/MemoryInterpreter.h"
#include "Wii/Diagnostics.h"
#if defined(WII_CPU_COMPILED_REFERENCE)
// The performance runner links the complete preceding MIPSTables object with
// isolated symbols, exporting only this entry point. Both dispatchers use -O3.
void PreviousCPUInterpret(MIPSOpcode op);
#else
namespace PreviousCpuMemory {using Wii::addressSpace;
// Keep uncommon unaligned word merging out of the ordinary load/store loop.
// This is shared code, not a decoded guest instruction cache.
__attribute__((noinline)) inline bool InterpretMemoryMerge(uint32_t op,uint32_t *r,uint32_t &pc,uint8_t *p) {
  const unsigned kind=op>>26,rt=(op>>16)&31;
  const uint32_t address=r[(op>>21)&31]+uint32_t(int32_t(int16_t(op)));
  uint32_t value;
  const unsigned shift=(address&3)*8;
  const uint32_t mem=*reinterpret_cast<const u32_le *>(p);
  if(kind==34)value=(r[rt]&(0x00ffffffu>>shift))|(mem<<(24-shift));
  else if(kind==38)value=(r[rt]&(0xffffff00u<<(24-shift)))|(mem>>shift);
  else {
    value=kind==42?(r[rt]>>(24-shift))|(mem&(0xffffff00u<<shift)):
      (r[rt]<<shift)|(mem&(0x00ffffffu>>(24-shift)));
    *reinterpret_cast<u32_le *>(p)=value;pc+=4;return true;
  }
  if(rt)r[rt]=value;pc+=4;return true;
}

// Ordinary mapped RAM, VRAM mirrors and scratchpad. I/O, synchronization,
// coprocessors and invalid/alignment cases retain their existing handlers.
inline bool InterpretMemory(uint32_t op,uint32_t *r,uint32_t &pc) {
  const unsigned kind=op>>26;
  static constexpr uint8_t widths[15]={1,2,4,4,1,2,4,0,1,2,4,4,0,0,4};
  if(kind<32||kind>46)return false;
  const unsigned bytes=widths[kind-32];
  if(!bytes)return false;
  const uint32_t address=r[(op>>21)&31]+uint32_t(int32_t(int16_t(op)));
  const bool merge=kind==34||kind==38||kind==42||kind==46;
  if(!merge&&(address&(bytes-1)))return false;
  uint8_t *p=addressSpace.Contiguous(merge?address&~3u:address,bytes);
  if(!p)return false;
  unsigned rt=(op>>16)&31;uint32_t value;
  if(merge)return InterpretMemoryMerge(op,r,pc,p);
  switch(kind) {
    case 32:value=uint32_t(int32_t(int8_t(*p)));break;
    case 33:value=uint32_t(int32_t(int16_t(*reinterpret_cast<const u16_le *>(p))));break;
    case 35:value=*reinterpret_cast<const u32_le *>(p);break;
    case 36:value=*p;break;
    case 37:value=*reinterpret_cast<const u16_le *>(p);break;
    case 40:*p=uint8_t(r[rt]);pc+=4;return true;
    case 41:*reinterpret_cast<u16_le *>(p)=uint16_t(r[rt]);pc+=4;return true;
    case 43:*reinterpret_cast<u32_le *>(p)=r[rt];pc+=4;return true;
    default:return false;
  }
  if(rt)r[rt]=value;pc+=4;return true;
}
}
#if PPSSPP_PLATFORM(WII)
// The normal and likely relative branches share a condition and completion
// path. Keep it compact and inline, avoiding a second opcode dispatch call.
// State updates match Int_RelBranch, including branches in a delay slot.
static inline void PreviousWiiInterpretRelBranch(MIPSOpcode op,unsigned kind) {
  const u32 lhs=currentMIPS->r[(op.encoding>>21)&31];
  bool taken;
  switch(kind&3) {
  case 0:taken=lhs==currentMIPS->r[(op.encoding>>16)&31];break;
  case 1:taken=lhs!=currentMIPS->r[(op.encoding>>16)&31];break;
  case 2:taken=int32_t(lhs)<=0;break;
  default:taken=int32_t(lhs)>0;break;
  }
  if(taken) {
    const u32 target=currentMIPS->pc+4+u32(int32_t(int16_t(op.encoding))*4);
    currentMIPS->pc+=4;
    mipsr4k.nextPC=target;
    mipsr4k.inDelaySlot=true;
  } else if(kind&16) {
    currentMIPS->pc+=8;
    --mipsr4k.downcount;
  } else currentMIPS->pc+=4;
}
static inline bool PreviousWiiInterpretFast(MIPSOpcode op) {
  const unsigned kind=op.encoding>>26;
  switch(kind) {
  case 2:case 3:
    MIPSInt::Int_JumpType(op);return true;
  case 4:case 5:case 6:case 7:
  case 20:case 21:case 22:case 23:
    PreviousWiiInterpretRelBranch(op,kind);return true;
  case 1:
    if((((op.encoding>>16)&31)&~19u)==0) {
      MIPSInt::Int_RelBranchRI(op);return true;
    }
    break;
  case 0:
    if((op.encoding&63)==8||(op.encoding&63)==9) {
      MIPSInt::Int_JumpRegType(op);return true;
    }
    if(Wii::InterpretScalar(op.encoding,currentMIPS->r,currentMIPS->pc))return true;
    return Wii::InterpretHiLo(op.encoding,currentMIPS->r,currentMIPS->lo,currentMIPS->hi,currentMIPS->pc);
  case 9:case 10:case 11:case 12:case 13:case 14:case 15:case 31:
    return Wii::InterpretScalar(op.encoding,currentMIPS->r,currentMIPS->pc);
  default:
    if(kind>=32)return PreviousCpuMemory::InterpretMemory(op.encoding,currentMIPS->r,currentMIPS->pc);
    break;
  }
  // Unsupported encodings and remaining coprocessors retain decoding.
  return false;
}
#endif
#if PPSSPP_PLATFORM(WII)
// Flatten the deeper decoder paths once. Operand registers/immediates remain
// live inputs to the original handlers; these tables contain no guest code.
static inline __attribute__((always_inline)) MIPSInterpretFunc PreviousWiiCachedHandler(MIPSOpcode op) {
  const unsigned kind=op.encoding>>26;
  if(kind==52||kind==60) {
    struct VectorTable {
      MIPSInterpretFunc handlers[2048];
      VectorTable() {
        for(unsigned i=0;i<2048;++i)
          handlers[i]=MIPSGetInterpretFunc(MIPSOpcode((i<1024?52u:60u)<<26|(i&1023)<<16));
      }
    };
    static const VectorTable table;
    return table.handlers[(kind==60?1024:0)+((op.encoding>>16)&1023)];
  }
  return MIPSGetInterpretFunc(op);
}
#if defined(PPSSPP_WII_PORTABLE_TEST)
MIPSInterpretFunc PreviousTestCachedHandler(MIPSOpcode op) {return PreviousWiiCachedHandler(op);}
#endif
#endif
static void PreviousMIPSInterpretSlow(MIPSOpcode op) {
#if PPSSPP_PLATFORM(WII)
  auto handler=PreviousWiiCachedHandler(op);
#else
  auto handler=MIPSGetInterpretFunc(op);
#endif
  if(handler) {
    handler(op);
	} else {
		ERROR_LOG_REPORT(CPU, "Unknown instruction %08x at %08x", op.encoding, currentMIPS->pc);
		// Try to disassemble it
		char disasm[256];
		MIPSDisAsm(op, currentMIPS->pc, disasm);
		_dbg_assert_msg_(CPU, 0, "%s", disasm);
		currentMIPS->pc += 4;
	}
}

__attribute__((noinline)) void PreviousCPUInterpret(MIPSOpcode op) {
#if PPSSPP_PLATFORM(WII)
  Wii::TraceInstruction(currentMIPS->pc,op.encoding,currentMIPS->r[29],currentMIPS->r[31]);
  if(PreviousWiiInterpretFast(op))return;
#endif
  PreviousMIPSInterpretSlow(op);
}


#endif
