#include "Wii/CachedInterpreter.h"
#include "Wii/CpuProfile.h"
// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include "Core/Core.h"
#include <algorithm>
#include "Core/System.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSDis.h"
#if PPSSPP_PLATFORM(WII)
#include "Wii/InstructionFetch.h"
#include "Wii/ScalarInterpreter.h"
#include "Wii/MemoryInterpreter.h"
#include "Wii/Diagnostics.h"
#include "Wii/Settings.h"
#include "Wii/Runtime.h"
#endif
#include "Core/MIPS/MIPSDisVFPU.h"
#include "Core/MIPS/MIPSInt.h"
#include "Core/MIPS/MIPSIntVFPU.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/CoreTiming.h"
#include "Core/Reporting.h"
#include "Core/Debugger/Breakpoints.h"
#include "base/logging.h"



enum MipsEncoding {
	Imme,
	Spec,
	Spe2,
	Spe3,
	RegI,
	Cop0,
	Cop0CO,
	Cop1,
	Cop1BC,
	Cop1S,
	Cop1W,
	Cop2,
	Cop2BC2,
	Cop2Rese,
	VFPU0,
	VFPU1,
	VFPU3,
	VFPU4Jump,
	VFPU7,
	VFPU4,
	VFPU5,
	VFPU6,
	VFPUMatrix1,
	VFPU9,
	ALLEGREX0,
	Emu,
	Rese,
	NumEncodings,

	Instruc = -1,
	Inval = -2,
};

struct MIPSInstruction {
	MipsEncoding altEncoding;
	const char *name;
	MIPSDisFunc disasm;
	MIPSInterpretFunc interpret;
	//MIPSInstructionInfo information;
	MIPSInfo flags;
};

#define INVALID {Inval}
#define INVALID_X_8 INVALID,INVALID,INVALID,INVALID,INVALID,INVALID,INVALID,INVALID

#define ENCODING(a) {a}
#define INSTR(name, comp, dis, inter, flags) {Instruc, name, dis, inter, MIPSInfo(flags)}


using namespace MIPSDis;
using namespace MIPSInt;



// regregreg instructions
const MIPSInstruction tableImmediate[64] = // xxxxxx ..... ..... ................
{
	//0
	ENCODING(Spec),
	ENCODING(RegI),
	INSTR("j",    0, Dis_JumpType, Int_JumpType, IS_JUMP|IN_IMM26|DELAYSLOT),
	INSTR("jal",  0, Dis_JumpType, Int_JumpType, IS_JUMP|IN_IMM26|OUT_RA|DELAYSLOT),
	INSTR("beq",  0, Dis_RelBranch2, Int_RelBranch, IS_CONDBRANCH|IN_IMM16|IN_RS|IN_RT|DELAYSLOT|CONDTYPE_EQ),
	INSTR("bne",  0, Dis_RelBranch2, Int_RelBranch, IS_CONDBRANCH|IN_IMM16|IN_RS|IN_RT|DELAYSLOT|CONDTYPE_NE),
	INSTR("blez", 0, Dis_RelBranch,  Int_RelBranch, IS_CONDBRANCH|IN_IMM16|IN_RS|DELAYSLOT|CONDTYPE_LEZ),
	INSTR("bgtz", 0, Dis_RelBranch,  Int_RelBranch, IS_CONDBRANCH|IN_IMM16|IN_RS|DELAYSLOT|CONDTYPE_GTZ),
	//8
	INSTR("addi",  0, Dis_addi,   Int_IType, IN_RS|IN_IMM16|OUT_RT),
	INSTR("addiu", 0, Dis_addi,   Int_IType, IN_RS|IN_IMM16|OUT_RT),
	INSTR("slti",  0, Dis_IType,  Int_IType, IN_RS|IN_IMM16|OUT_RT),
	INSTR("sltiu", 0, Dis_IType,  Int_IType, IN_RS|IN_IMM16|OUT_RT),
	INSTR("andi",  0, Dis_IType,  Int_IType, IN_RS|IN_IMM16|OUT_RT),
	INSTR("ori",   0, Dis_ori,    Int_IType, IN_RS|IN_IMM16|OUT_RT),
	INSTR("xori",  0, Dis_IType,  Int_IType, IN_RS|IN_IMM16|OUT_RT),
	INSTR("lui",   0, Dis_IType1, Int_IType, IN_IMM16|OUT_RT),
	//16
	ENCODING(Cop0), //cop0
	ENCODING(Cop1), //cop1
	ENCODING(Cop2), //cop2
	INVALID, //copU

	INSTR("beql",  0, Dis_RelBranch2, Int_RelBranch, IS_CONDBRANCH|IN_IMM16|IN_RS|IN_RT|DELAYSLOT|LIKELY|CONDTYPE_EQ), //L = likely
	INSTR("bnel",  0, Dis_RelBranch2, Int_RelBranch, IS_CONDBRANCH|IN_IMM16|IN_RS|IN_RT|DELAYSLOT|LIKELY|CONDTYPE_NE),
	INSTR("blezl", 0, Dis_RelBranch,  Int_RelBranch, IS_CONDBRANCH|IN_IMM16|IN_RS|DELAYSLOT|LIKELY|CONDTYPE_LEZ),
	INSTR("bgtzl", 0, Dis_RelBranch,  Int_RelBranch, IS_CONDBRANCH|IN_IMM16|IN_RS|DELAYSLOT|LIKELY|CONDTYPE_GTZ),
	//24
	ENCODING(VFPU0),
	ENCODING(VFPU1),
	ENCODING(Emu),
	ENCODING(VFPU3),
	ENCODING(Spe2), //special2
	INVALID,
	INVALID,
	ENCODING(Spe3), //special3
	//32
	INSTR("lb",  0, Dis_ITypeMem, Int_ITypeMem, IN_MEM|IN_IMM16|IN_RS_ADDR|OUT_RT|MEMTYPE_BYTE),
	INSTR("lh",  0, Dis_ITypeMem, Int_ITypeMem, IN_MEM|IN_IMM16|IN_RS_ADDR|OUT_RT|MEMTYPE_HWORD),
	INSTR("lwl", 0, Dis_ITypeMem, Int_ITypeMem, IN_MEM|IN_IMM16|IN_RS_ADDR|IN_RT|OUT_RT|MEMTYPE_WORD),
	INSTR("lw",  0, Dis_ITypeMem, Int_ITypeMem, IN_MEM|IN_IMM16|IN_RS_ADDR|OUT_RT|MEMTYPE_WORD),
	INSTR("lbu", 0, Dis_ITypeMem, Int_ITypeMem, IN_MEM|IN_IMM16|IN_RS_ADDR|OUT_RT|MEMTYPE_BYTE),
	INSTR("lhu", 0, Dis_ITypeMem, Int_ITypeMem, IN_MEM|IN_IMM16|IN_RS_ADDR|OUT_RT|MEMTYPE_HWORD),
	INSTR("lwr", 0, Dis_ITypeMem, Int_ITypeMem, IN_MEM|IN_IMM16|IN_RS_ADDR|IN_RT|OUT_RT|MEMTYPE_WORD),
	INVALID,
	//40
	INSTR("sb",  0, Dis_ITypeMem, Int_ITypeMem, IN_IMM16|IN_RS_ADDR|IN_RT|OUT_MEM|MEMTYPE_BYTE),
	INSTR("sh",  0, Dis_ITypeMem, Int_ITypeMem, IN_IMM16|IN_RS_ADDR|IN_RT|OUT_MEM|MEMTYPE_HWORD),
	INSTR("swl", 0, Dis_ITypeMem, Int_ITypeMem, IN_IMM16|IN_RS_ADDR|IN_RT|OUT_MEM|MEMTYPE_WORD),
	INSTR("sw",  0, Dis_ITypeMem, Int_ITypeMem, IN_IMM16|IN_RS_ADDR|IN_RT|OUT_MEM|MEMTYPE_WORD),
	INVALID,
	INVALID,
	INSTR("swr", 0, Dis_ITypeMem, Int_ITypeMem, IN_IMM16|IN_RS_ADDR|IN_RT|OUT_MEM|MEMTYPE_WORD),
	INSTR("cache", 0, Dis_Cache, Int_Cache, IN_MEM|IN_IMM16|IN_RS_ADDR),
	//48
	INSTR("ll", 0, Dis_Generic, Int_StoreSync, IN_MEM|IN_IMM16|IN_RS_ADDR|OUT_RT|OUT_OTHER|MEMTYPE_WORD),
	INSTR("lwc1", 0, Dis_FPULS, Int_FPULS, IN_MEM|IN_IMM16|IN_RS_ADDR|OUT_FT|MEMTYPE_FLOAT|IS_FPU),
	INSTR("lv.s", 0, Dis_SV, Int_SV, IN_MEM|IN_IMM16|IN_RS_ADDR|OUT_OTHER|IS_VFPU|VFPU_NO_PREFIX|MEMTYPE_FLOAT),
	INVALID,
	ENCODING(VFPU4Jump),
	INSTR("lv", 0, Dis_SVLRQ, Int_SVQ, IN_MEM|IN_IMM16|IN_RS_ADDR|OUT_OTHER|IS_VFPU|VFPU_NO_PREFIX|MEMTYPE_VQUAD),
	INSTR("lv.q", 0, Dis_SVQ, Int_SVQ, IN_MEM|IN_IMM16|IN_RS_ADDR|OUT_OTHER|IS_VFPU|VFPU_NO_PREFIX|MEMTYPE_VQUAD), //copU
	ENCODING(VFPU5),
	//56
	INSTR("sc", 0, Dis_Generic, Int_StoreSync, IN_IMM16|IN_RS_ADDR|IN_OTHER|IN_RT|OUT_RT|OUT_MEM|MEMTYPE_WORD),
	INSTR("swc1", 0, Dis_FPULS, Int_FPULS, IN_IMM16|IN_RS_ADDR|IN_FT|OUT_MEM|MEMTYPE_FLOAT|IS_FPU), //copU
	INSTR("sv.s", 0, Dis_SV, Int_SV, IN_IMM16|IN_RS_ADDR|IN_OTHER|OUT_MEM|IS_VFPU|VFPU_NO_PREFIX|MEMTYPE_FLOAT),
	INVALID,
	//60
	ENCODING(VFPU6),
	INSTR("sv", 0, Dis_SVLRQ, Int_SVQ, IN_IMM16|IN_RS_ADDR|IN_OTHER|OUT_MEM|IS_VFPU|VFPU_NO_PREFIX|MEMTYPE_VQUAD), //copU
	INSTR("sv.q", 0, Dis_SVQ, Int_SVQ, IN_IMM16|IN_RS_ADDR|IN_OTHER|OUT_MEM|IS_VFPU|VFPU_NO_PREFIX|MEMTYPE_VQUAD),
	// Some call this VFPU7 (vflush/vnop/vsync), but it's not super important.
	INSTR("vflush", 0, Dis_Vflush, Int_Vflush, IS_VFPU|VFPU_NO_PREFIX),
};

const MIPSInstruction tableSpecial[64] = // 000000 ..... ..... ..... ..... xxxxxx
{
	INSTR("sll",   0, Dis_ShiftType, Int_ShiftType, OUT_RD|IN_RT|IN_SA),
	INVALID,  // copu

	INSTR("srl",   0, Dis_ShiftType, Int_ShiftType, OUT_RD|IN_RT|IN_SA),
	INSTR("sra",   0, Dis_ShiftType, Int_ShiftType, OUT_RD|IN_RT|IN_SA),
	INSTR("sllv",  0, Dis_VarShiftType, Int_ShiftType, OUT_RD|IN_RT|IN_RS_SHIFT),
	INVALID,
	INSTR("srlv",  0, Dis_VarShiftType, Int_ShiftType, OUT_RD|IN_RT|IN_RS_SHIFT),
	INSTR("srav",  0, Dis_VarShiftType, Int_ShiftType, OUT_RD|IN_RT|IN_RS_SHIFT),

	//8
	INSTR("jr",    0, Dis_JumpRegType, Int_JumpRegType, IS_JUMP|IN_RS|DELAYSLOT),
	INSTR("jalr",  0, Dis_JumpRegType, Int_JumpRegType, IS_JUMP|IN_RS|OUT_RD|DELAYSLOT),
	INSTR("movz",  0, Dis_RType3, Int_RType3, OUT_RD|IN_RS|IN_RT|IS_CONDMOVE|CONDTYPE_EQ),
	INSTR("movn",  0, Dis_RType3, Int_RType3, OUT_RD|IN_RS|IN_RT|IS_CONDMOVE|CONDTYPE_NE),
	INSTR("syscall", 0, Dis_Syscall, Int_Syscall, IN_MEM|IN_OTHER|OUT_MEM|OUT_OTHER),
	INSTR("break", 0, Dis_Generic, Int_Break, 0),
	INVALID,
	INSTR("sync",  0, Dis_Generic, Int_Sync, 0),

	//16
	INSTR("mfhi",  0, Dis_FromHiloTransfer, Int_MulDivType, OUT_RD|IN_HI),
	INSTR("mthi",  0, Dis_ToHiloTransfer,   Int_MulDivType, IN_RS|OUT_HI),
	INSTR("mflo",  0, Dis_FromHiloTransfer, Int_MulDivType, OUT_RD|IN_LO),
	INSTR("mtlo",  0, Dis_ToHiloTransfer,   Int_MulDivType, IN_RS|OUT_LO),
	INVALID,
	INVALID,
	INSTR("clz",   0, Dis_RType2, Int_RType2, OUT_RD|IN_RS),
	INSTR("clo",   0, Dis_RType2, Int_RType2, OUT_RD|IN_RS),

	//24
	INSTR("mult",  0, Dis_MulDivType, Int_MulDivType, IN_RS|IN_RT|OUT_HI|OUT_LO),
	INSTR("multu", 0, Dis_MulDivType, Int_MulDivType, IN_RS|IN_RT|OUT_HI|OUT_LO),
	INSTR("div",   0, Dis_MulDivType, Int_MulDivType, IN_RS|IN_RT|OUT_HI|OUT_LO),
	INSTR("divu",  0, Dis_MulDivType, Int_MulDivType, IN_RS|IN_RT|OUT_HI|OUT_LO),
	INSTR("madd",  0, Dis_MulDivType, Int_MulDivType, IN_RS|IN_RT|IN_HI|IN_LO|OUT_HI|OUT_LO),
	INSTR("maddu", 0, Dis_MulDivType, Int_MulDivType, IN_RS|IN_RT|IN_HI|IN_LO|OUT_HI|OUT_LO),
	INVALID,
	INVALID,

	//32
	INSTR("add",  0, Dis_RType3, Int_RType3, IN_RS|IN_RT|OUT_RD),
	INSTR("addu", 0, Dis_addu,   Int_RType3, IN_RS|IN_RT|OUT_RD),
	INSTR("sub",  0, Dis_RType3, Int_RType3, IN_RS|IN_RT|OUT_RD),
	INSTR("subu", 0, Dis_RType3, Int_RType3, IN_RS|IN_RT|OUT_RD),
	INSTR("and",  0, Dis_RType3, Int_RType3, IN_RS|IN_RT|OUT_RD),
	INSTR("or",   0, Dis_addu,   Int_RType3, IN_RS|IN_RT|OUT_RD),
	INSTR("xor",  0, Dis_RType3, Int_RType3, IN_RS|IN_RT|OUT_RD),
	INSTR("nor",  0, Dis_RType3, Int_RType3, IN_RS|IN_RT|OUT_RD),

	//40
	INVALID,
	INVALID,
	INSTR("slt",  0, Dis_RType3, Int_RType3, IN_RS|IN_RT|OUT_RD),
	INSTR("sltu", 0, Dis_RType3, Int_RType3, IN_RS|IN_RT|OUT_RD),
	INSTR("max",  0, Dis_RType3, Int_RType3, IN_RS|IN_RT|OUT_RD),
	INSTR("min",  0, Dis_RType3, Int_RType3, IN_RS|IN_RT|OUT_RD),
	INSTR("msub",  0, Dis_MulDivType, Int_MulDivType, IN_RS|IN_RT|IN_HI|IN_LO|OUT_HI|OUT_LO),
	INSTR("msubu", 0, Dis_MulDivType, Int_MulDivType, IN_RS|IN_RT|IN_HI|IN_LO|OUT_HI|OUT_LO),

	//48
	INSTR("tge",  0, Dis_RType3, 0, 0),
	INSTR("tgeu", 0, Dis_RType3, 0, 0),
	INSTR("tlt",  0, Dis_RType3, 0, 0),
	INSTR("tltu", 0, Dis_RType3, 0, 0),
	INSTR("teq",  0, Dis_RType3, 0, 0),
	INVALID,
	INSTR("tne",  0, Dis_RType3, 0, 0),
	INVALID,

	//56
	INVALID_X_8,
};

// Theoretically should not hit these.
const MIPSInstruction tableSpecial2[64] = // 011100 ..... ..... ..... ..... xxxxxx
{
	INSTR("halt", 0, Dis_Generic, 0, 0),
	INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID,
	//8
	INVALID_X_8,
	INVALID_X_8,
	INVALID_X_8,
	//32
	INVALID, INVALID, INVALID, INVALID,
	INSTR("mfic", 0, Dis_Generic, Int_Special2, OUT_OTHER),
	INVALID,
	INSTR("mtic", 0, Dis_Generic, Int_Special2, OUT_OTHER),
	INVALID,
	//40
	INVALID_X_8,
	INVALID_X_8,
	INVALID_X_8,
};

const MIPSInstruction tableSpecial3[64] = // 011111 ..... ..... ..... ..... xxxxxx
{
	INSTR("ext", 0, Dis_Special3, Int_Special3, IN_RS|OUT_RT),
	INVALID,
	INVALID,
	INVALID,
	INSTR("ins", 0, Dis_Special3, Int_Special3, IN_RS|IN_RT|OUT_RT),
	INVALID,
	INVALID,
	INVALID,
	//8
	INVALID_X_8,
	//16
	INVALID_X_8,
	//24
	// TODO: Is this right?  Or should it only be 32?  Comment above (24) was mistakenly 32 before.
	ENCODING(ALLEGREX0),
	INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID,
	//32
	ENCODING(ALLEGREX0),
	INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID,
	//40
	INVALID_X_8,
	INVALID_X_8,
	//56
	INVALID, INVALID, INVALID,
	INSTR("rdhwr", 0, Dis_Generic, 0, 0),
	INVALID, INVALID, INVALID, INVALID,
};

const MIPSInstruction tableRegImm[32] = // 000001 ..... xxxxx ................
{
	INSTR("bltz",  0, Dis_RelBranch, Int_RelBranchRI, IS_CONDBRANCH|IN_IMM16|IN_RS|DELAYSLOT|CONDTYPE_LTZ),
	INSTR("bgez",  0, Dis_RelBranch, Int_RelBranchRI, IS_CONDBRANCH|IN_IMM16|IN_RS|DELAYSLOT|CONDTYPE_GEZ),
	INSTR("bltzl", 0, Dis_RelBranch, Int_RelBranchRI, IS_CONDBRANCH|IN_IMM16|IN_RS|DELAYSLOT|LIKELY|CONDTYPE_LTZ),
	INSTR("bgezl", 0, Dis_RelBranch, Int_RelBranchRI, IS_CONDBRANCH|IN_IMM16|IN_RS|DELAYSLOT|LIKELY|CONDTYPE_GEZ),
	INVALID,
	INVALID,
	INVALID,
	INVALID,
	//8
	INSTR("tgei",  0, Dis_Generic, 0, 0),
	INSTR("tgeiu", 0, Dis_Generic, 0, 0),
	INSTR("tlti",  0, Dis_Generic, 0, 0),
	INSTR("tltiu", 0, Dis_Generic, 0, 0),
	INSTR("teqi",  0, Dis_Generic, 0, 0),
	INVALID,
	INSTR("tnei",  0, Dis_Generic, 0, 0),
	INVALID,
	//16
	INSTR("bltzal",  0, Dis_RelBranch, Int_RelBranchRI, IS_CONDBRANCH|IN_IMM16|IN_RS|OUT_RA|DELAYSLOT|CONDTYPE_LTZ),
	INSTR("bgezal",  0, Dis_RelBranch, Int_RelBranchRI, IS_CONDBRANCH|IN_IMM16|IN_RS|OUT_RA|DELAYSLOT|CONDTYPE_GEZ),
	INSTR("bltzall", 0, Dis_RelBranch, Int_RelBranchRI, IS_CONDBRANCH|IN_IMM16|IN_RS|OUT_RA|DELAYSLOT|LIKELY|CONDTYPE_LTZ), //L = likely
	INSTR("bgezall", 0, Dis_RelBranch, Int_RelBranchRI, IS_CONDBRANCH|IN_IMM16|IN_RS|OUT_RA|DELAYSLOT|LIKELY|CONDTYPE_GEZ),
	INVALID,
	INVALID,
	INVALID,
	INVALID,
	//24
	INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID,
	INSTR("synci", 0, Dis_Generic, 0, 0),
};

const MIPSInstruction tableCop2[32] = // 010010 xxxxx ..... ................
{
	INSTR("mfc2", 0, Dis_Generic, 0, OUT_RT),
	INVALID,
	INSTR("cfc2", 0, Dis_Generic, 0, 0),
	INSTR("mfv", 0, Dis_Mftv, Int_Mftv, IN_OTHER|IN_VFPU_CC|OUT_RT|IS_VFPU),
	INSTR("mtc2", 0, Dis_Generic, 0, IN_RT),
	INVALID,
	INSTR("ctc2", 0, Dis_Generic, 0, 0),
	INSTR("mtv", 0, Dis_Mftv, Int_Mftv, IN_RT|OUT_VFPU_CC|OUT_OTHER|IS_VFPU),
	//8
	ENCODING(Cop2BC2),
	INSTR("??", 0, Dis_Generic, 0, 0),
	INSTR("??", 0, Dis_Generic, 0, 0),
	INSTR("??", 0, Dis_Generic, 0, 0),
	INSTR("??", 0, Dis_Generic, 0, 0),
	INSTR("??", 0, Dis_Generic, 0, 0),
	INSTR("??", 0, Dis_Generic, 0, 0),
	INSTR("??", 0, Dis_Generic, 0, 0),
	//16
	INVALID_X_8,
	INVALID_X_8,
};

const MIPSInstruction tableCop2BC2[4] = // 010010 01000 ...xx ................
{
	INSTR("bvf", 0, Dis_VBranch, Int_VBranch, IS_CONDBRANCH|IN_IMM16|IN_VFPU_CC|DELAYSLOT|IS_VFPU),
	INSTR("bvt", 0, Dis_VBranch, Int_VBranch, IS_CONDBRANCH|IN_IMM16|IN_VFPU_CC|DELAYSLOT|IS_VFPU),
	INSTR("bvfl", 0, Dis_VBranch, Int_VBranch, IS_CONDBRANCH|IN_IMM16|IN_VFPU_CC|DELAYSLOT|LIKELY|IS_VFPU),
	INSTR("bvtl", 0, Dis_VBranch, Int_VBranch, IS_CONDBRANCH|IN_IMM16|IN_VFPU_CC|DELAYSLOT|LIKELY|IS_VFPU),
};

const MIPSInstruction tableCop0[32] = // 010000 xxxxx ..... ................
{
	INSTR("mfc0", 0, Dis_Generic, 0, OUT_RT),  // unused
	INVALID,
	INVALID,
	INVALID,
	INSTR("mtc0", 0, Dis_Generic, 0, IN_RT),  // unused
	INVALID,
	INVALID,
	INVALID,
	//8
	INVALID,
	INVALID,
	INSTR("rdpgpr", 0, Dis_Generic, 0, 0),
	INSTR("mfmc0", 0, Dis_Generic, 0, 0),

	INVALID,
	INVALID,
	INSTR("wrpgpr", 0, Dis_Generic, 0, 0),
	INVALID,
	//16
	ENCODING(Cop0CO), ENCODING(Cop0CO), ENCODING(Cop0CO), ENCODING(Cop0CO), ENCODING(Cop0CO), ENCODING(Cop0CO), ENCODING(Cop0CO), ENCODING(Cop0CO),
	ENCODING(Cop0CO), ENCODING(Cop0CO), ENCODING(Cop0CO), ENCODING(Cop0CO), ENCODING(Cop0CO), ENCODING(Cop0CO), ENCODING(Cop0CO), ENCODING(Cop0CO),
};

// we won't encounter these since we only do user mode emulation
const MIPSInstruction tableCop0CO[64] = // 010000 1.... ..... ..... ..... xxxxxx
{
	INVALID,
	INSTR("tlbr", 0, Dis_Generic, 0, 0),
	INSTR("tlbwi", 0, Dis_Generic, 0, 0),
	INVALID,
	INVALID,
	INVALID,
	INSTR("tlbwr", 0, Dis_Generic, 0, 0),
	INVALID,
	//8
	INSTR("tlbp", 0, Dis_Generic, 0, 0),
	INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID,
	INVALID_X_8,
	//24
	INSTR("eret", 0, Dis_Generic, 0, 0),
	INSTR("iack", 0, Dis_Generic, 0, 0),
	INVALID, INVALID, INVALID, INVALID, INVALID,
	INSTR("deret", 0, Dis_Generic, 0, 0),
	//32
	INSTR("wait", 0, Dis_Generic, 0, 0),
	INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID,
	//40
	INVALID_X_8,
	INVALID_X_8,
	INVALID_X_8,
};

const MIPSInstruction tableCop1[32] = // 010001 xxxxx ..... ..... ...........
{
	INSTR("mfc1", 0, Dis_mxc1, Int_mxc1, IN_FS|OUT_RT|IS_FPU),
	INVALID,
	INSTR("cfc1", 0, Dis_mxc1, Int_mxc1, IN_OTHER|IN_FPUFLAG|OUT_RT|IS_FPU),
	INVALID,
	INSTR("mtc1", 0, Dis_mxc1, Int_mxc1, IN_RT|OUT_FS|IS_FPU),
	INVALID,
	INSTR("ctc1", 0, Dis_mxc1, Int_mxc1, IN_RT|OUT_FPUFLAG|OUT_OTHER|IS_FPU),
	INVALID,
	//8
	ENCODING(Cop1BC), INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID,
	//16
	ENCODING(Cop1S), INVALID, INVALID, INVALID,
	ENCODING(Cop1W), INVALID, INVALID, INVALID,
	//24
	INVALID_X_8,
};

const MIPSInstruction tableCop1BC[32] = // 010001 01000 xxxxx ................
{
	INSTR("bc1f",  0, Dis_FPUBranch, Int_FPUBranch, IS_CONDBRANCH|IN_IMM16|IN_FPUFLAG|DELAYSLOT|CONDTYPE_FPUFALSE|IS_FPU),
	INSTR("bc1t",  0, Dis_FPUBranch, Int_FPUBranch, IS_CONDBRANCH|IN_IMM16|IN_FPUFLAG|DELAYSLOT|CONDTYPE_FPUTRUE|IS_FPU),
	INSTR("bc1fl", 0, Dis_FPUBranch, Int_FPUBranch, IS_CONDBRANCH|IN_IMM16|IN_FPUFLAG|DELAYSLOT|LIKELY|CONDTYPE_FPUFALSE|IS_FPU),
	INSTR("bc1tl", 0, Dis_FPUBranch, Int_FPUBranch, IS_CONDBRANCH|IN_IMM16|IN_FPUFLAG|DELAYSLOT|LIKELY|CONDTYPE_FPUTRUE|IS_FPU),
	INVALID, INVALID, INVALID, INVALID,
	//8
	INVALID_X_8,
	INVALID_X_8,
	INVALID_X_8,
};

const MIPSInstruction tableCop1S[64] = // 010001 10000 ..... ..... ..... xxxxxx
{
	INSTR("add.s",  0, Dis_FPU3op, Int_FPU3op, OUT_FD|IN_FS|IN_FT|IS_FPU),
	INSTR("sub.s",  0, Dis_FPU3op, Int_FPU3op, OUT_FD|IN_FS|IN_FT|IS_FPU),
	INSTR("mul.s",  0, Dis_FPU3op, Int_FPU3op, OUT_FD|IN_FS|IN_FT|IS_FPU),
	INSTR("div.s",  0, Dis_FPU3op, Int_FPU3op, OUT_FD|IN_FS|IN_FT|IS_FPU),
	INSTR("sqrt.s", 0, Dis_FPU2op, Int_FPU2op, OUT_FD|IN_FS|IS_FPU),
	INSTR("abs.s",  0, Dis_FPU2op, Int_FPU2op, OUT_FD|IN_FS|IS_FPU),
	INSTR("mov.s",  0, Dis_FPU2op, Int_FPU2op, OUT_FD|IN_FS|IS_FPU),
	INSTR("neg.s",  0, Dis_FPU2op, Int_FPU2op, OUT_FD|IN_FS|IS_FPU),
	//8
	INVALID, INVALID, INVALID, INVALID,
	INSTR("round.w.s",  0, Dis_FPU2op, Int_FPU2op, OUT_FD|IN_FS|IS_FPU),
	INSTR("trunc.w.s",  0, Dis_FPU2op, Int_FPU2op, OUT_FD|IN_FS|IS_FPU),
	INSTR("ceil.w.s",   0, Dis_FPU2op, Int_FPU2op, OUT_FD|IN_FS|IS_FPU),
	INSTR("floor.w.s",  0, Dis_FPU2op, Int_FPU2op, OUT_FD|IN_FS|IS_FPU),
	//16
	INVALID_X_8,
	//24
	INVALID_X_8,
	//32
	INVALID, INVALID, INVALID, INVALID,
	//36
	INSTR("cvt.w.s", 0, Dis_FPU2op, Int_FPU2op, OUT_FD|IN_FS|IS_FPU),
	INVALID,
	INSTR("dis.int", 0, Dis_Generic, Int_Interrupt, 0),
	INVALID,
	//40
	INVALID_X_8,
	//48 - 010001 10000 ..... ..... ..... 11xxxx
	INSTR("c.f",   0, Dis_FPUComp, Int_FPUComp, OUT_FPUFLAG|IS_FPU),
	INSTR("c.un",  0, Dis_FPUComp, Int_FPUComp, IN_FS|IN_FT|OUT_FPUFLAG|IS_FPU),
	INSTR("c.eq",  0, Dis_FPUComp, Int_FPUComp, IN_FS|IN_FT|OUT_FPUFLAG|IS_FPU),
	INSTR("c.ueq", 0, Dis_FPUComp, Int_FPUComp, IN_FS|IN_FT|OUT_FPUFLAG|IS_FPU),
	INSTR("c.olt", 0, Dis_FPUComp, Int_FPUComp, IN_FS|IN_FT|OUT_FPUFLAG|IS_FPU),
	INSTR("c.ult", 0, Dis_FPUComp, Int_FPUComp, IN_FS|IN_FT|OUT_FPUFLAG|IS_FPU),
	INSTR("c.ole", 0, Dis_FPUComp, Int_FPUComp, IN_FS|IN_FT|OUT_FPUFLAG|IS_FPU),
	INSTR("c.ule", 0, Dis_FPUComp, Int_FPUComp, IN_FS|IN_FT|OUT_FPUFLAG|IS_FPU),
	INSTR("c.sf",  0, Dis_FPUComp, Int_FPUComp, OUT_FPUFLAG|IS_FPU),
	INSTR("c.ngle",0, Dis_FPUComp, Int_FPUComp, IN_FS|IN_FT|OUT_FPUFLAG|IS_FPU),
	INSTR("c.seq", 0, Dis_FPUComp, Int_FPUComp, IN_FS|IN_FT|OUT_FPUFLAG|IS_FPU),
	INSTR("c.ngl", 0, Dis_FPUComp, Int_FPUComp, IN_FS|IN_FT|OUT_FPUFLAG|IS_FPU),
	INSTR("c.lt",  0, Dis_FPUComp, Int_FPUComp, IN_FS|IN_FT|OUT_FPUFLAG|IS_FPU),
	INSTR("c.nge", 0, Dis_FPUComp, Int_FPUComp, IN_FS|IN_FT|OUT_FPUFLAG|IS_FPU),
	INSTR("c.le",  0, Dis_FPUComp, Int_FPUComp, IN_FS|IN_FT|OUT_FPUFLAG|IS_FPU),
	INSTR("c.ngt", 0, Dis_FPUComp, Int_FPUComp, IN_FS|IN_FT|OUT_FPUFLAG|IS_FPU),
};

const MIPSInstruction tableCop1W[64] = // 010001 10100 ..... ..... ..... xxxxxx
{
	INVALID_X_8,
	//8
	INVALID_X_8,
	//16
	INVALID_X_8,
	//24
	INVALID_X_8,
	//32
	INSTR("cvt.s.w", 0, Dis_FPU2op, Int_FPU2op, OUT_FD|IN_FS|IS_FPU),
	INVALID, INVALID, INVALID,
	//36
	INVALID,
	INVALID,
	INVALID,
	INVALID,
	//40
	INVALID_X_8,
	//48
	INVALID_X_8,
	INVALID_X_8,
};

const MIPSInstruction tableVFPU0[8] = // 011000 xxx ....... . ....... . .......
{
	INSTR("vadd", 0, Dis_VectorSet3, Int_VecDo3, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vsub", 0, Dis_VectorSet3, Int_VecDo3, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	// TODO: Flags may not be correct (prefixes, etc.)
	INSTR("vsbn", 0, Dis_VectorSet3, Int_Vsbn, IN_OTHER|OUT_OTHER|IS_VFPU),
	INVALID, INVALID, INVALID, INVALID,

	INSTR("vdiv", 0, Dis_VectorSet3, Int_VecDo3, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
};

const MIPSInstruction tableVFPU1[8] = // 011001 xxx ....... . ....... . .......
{
	INSTR("vmul", 0, Dis_VectorSet3, Int_VecDo3, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vdot", 0, Dis_VectorDot, Int_VDot, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vscl", 0, Dis_VScl, Int_VScl, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INVALID,
	INSTR("vhdp", 0, Dis_VectorDot, Int_VHdp, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vcrs", 0, Dis_Vcrs, Int_Vcrs, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vdet", 0, Dis_VectorDot, Int_Vdet, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INVALID,
};

const MIPSInstruction tableVFPU3[8] = // 011011 xxx ....... . ....... . .......
{
	INSTR("vcmp", 0, Dis_Vcmp, Int_Vcmp, IN_OTHER|OUT_VFPU_CC|IS_VFPU|OUT_EAT_PREFIX),
	INVALID,
	INSTR("vmin", 0, Dis_VectorSet3, Int_Vminmax, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vmax", 0, Dis_VectorSet3, Int_Vminmax, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INVALID,
	INSTR("vscmp", 0, Dis_VectorSet3, Int_Vscmp, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vsge", 0, Dis_VectorSet3, Int_Vsge, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vslt", 0, Dis_VectorSet3, Int_Vslt, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
};

const MIPSInstruction tableVFPU4Jump[32] = // 110100 xxxxx ..... . ....... . .......
{
	ENCODING(VFPU4),
	ENCODING(VFPU7),
	ENCODING(VFPU9),
	INSTR("vcst", 0, Dis_Vcst, Int_Vcst, OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INVALID, INVALID, INVALID, INVALID,

	//8
	INVALID_X_8,

	//16
	INSTR("vf2in", 0, Dis_Vf2i, Int_Vf2i, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vf2iz", 0, Dis_Vf2i, Int_Vf2i, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vf2iu", 0, Dis_Vf2i, Int_Vf2i, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vf2id", 0, Dis_Vf2i, Int_Vf2i, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	//20
	INSTR("vi2f", 0, Dis_Vf2i, Int_Vi2f, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vcmov", 0, Dis_Vcmov, Int_Vcmov, IN_OTHER|IN_VFPU_CC|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INVALID,
	INVALID,
	//24 - 110100 11 ........ . ....... . .......
	// TODO: Flags may not be correct (prefixes, etc.)
	INSTR("vwbn.s", 0, Dis_Vwbn, Int_Vwbn, IN_OTHER|OUT_OTHER|IS_VFPU),
	INSTR("vwbn.s", 0, Dis_Vwbn, Int_Vwbn, IN_OTHER|OUT_OTHER|IS_VFPU),
	INSTR("vwbn.s", 0, Dis_Vwbn, Int_Vwbn, IN_OTHER|OUT_OTHER|IS_VFPU),
	INSTR("vwbn.s", 0, Dis_Vwbn, Int_Vwbn, IN_OTHER|OUT_OTHER|IS_VFPU),
	INSTR("vwbn.s", 0, Dis_Vwbn, Int_Vwbn, IN_OTHER|OUT_OTHER|IS_VFPU),
	INSTR("vwbn.s", 0, Dis_Vwbn, Int_Vwbn, IN_OTHER|OUT_OTHER|IS_VFPU),
	INSTR("vwbn.s", 0, Dis_Vwbn, Int_Vwbn, IN_OTHER|OUT_OTHER|IS_VFPU),
	INSTR("vwbn.s", 0, Dis_Vwbn, Int_Vwbn, IN_OTHER|OUT_OTHER|IS_VFPU),
};

const MIPSInstruction tableVFPU7[32] = // 110100 00001 xxxxx . ....... . .......
{
	// TODO disasm
	INSTR("vrnds", 0, Dis_Vrnds, Int_Vrnds, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vrndi", 0, Dis_VrndX, Int_VrndX, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vrndf1", 0, Dis_VrndX, Int_VrndX, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vrndf2", 0, Dis_VrndX, Int_VrndX, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),

	INVALID, INVALID, INVALID, INVALID,
	//8
	INVALID, INVALID, INVALID, INVALID,
	// TODO: Flags may not be correct (prefixes, etc.)  Is this the correct encoding?  Others say 10110.
	INSTR("vsbz", 0, Dis_Generic, Int_Vsbz, IN_OTHER|OUT_OTHER|IS_VFPU),
	INVALID, INVALID, INVALID,
	//16
	INVALID,
	INVALID,
	INSTR("vf2h", 0, Dis_Vf2h, Int_Vf2h, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vh2f", 0, Dis_Vh2f, Int_Vh2f, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),

	INVALID,
	INVALID,
	INVALID,
	// TODO: Flags may not be correct (prefixes, etc.)
	INSTR("vlgb", 0, Dis_Generic, Int_Vlgb, IN_OTHER|OUT_OTHER|IS_VFPU),
	//24
	INSTR("vuc2i", 0, Dis_Vs2i, Int_Vx2i, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),  // Seen in BraveStory, initialization  110100 00001110000 000 0001 0000 0000
	INSTR("vc2i",  0, Dis_Vs2i, Int_Vx2i, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vus2i", 0, Dis_Vs2i, Int_Vx2i, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vs2i",  0, Dis_Vs2i, Int_Vx2i, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),

	INSTR("vi2uc", 0, Dis_Vi2x, Int_Vi2x, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vi2c",  0, Dis_Vi2x, Int_Vi2x, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vi2us", 0, Dis_Vi2x, Int_Vi2x, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vi2s",  0, Dis_Vi2x, Int_Vi2x, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
};

// 110100 00000 10100 0000000000000000
// 110100 00000 10111 0000000000000000
const MIPSInstruction tableVFPU4[32] = // 110100 00000 xxxxx . ....... . .......
{
	INSTR("vmov", 0, Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vabs", 0, Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vneg", 0, Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vidt", 0, Dis_VectorSet1, Int_Vidt, OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vsat0", 0, Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vsat1", 0, Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vzero", 0, Dis_VectorSet1, Int_VVectorInit, OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vone",  0, Dis_VectorSet1, Int_VVectorInit, OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	//8
	INVALID_X_8,
	//16
	INSTR("vrcp", 0, Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vrsq", 0, Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vsin", 0, Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vcos", 0, Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vexp2", 0, Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vlog2", 0, Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vsqrt", 0, Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vasin", 0, Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	//24
	INSTR("vnrcp", 0, Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INVALID,
	INSTR("vnsin", 0, Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INVALID,
	INSTR("vrexp2", 0, Dis_VectorSet2, Int_VV2Op, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INVALID, INVALID, INVALID,
};

MIPSInstruction tableVFPU5[8] = // 110111 xxx ....... ................
{
	INSTR("vpfxs", 0, Dis_VPFXST, Int_VPFX, IN_IMM16|OUT_OTHER|IS_VFPU),
	INSTR("vpfxs", 0, Dis_VPFXST, Int_VPFX, IN_IMM16|OUT_OTHER|IS_VFPU),
	INSTR("vpfxt", 0, Dis_VPFXST, Int_VPFX, IN_IMM16|OUT_OTHER|IS_VFPU),
	INSTR("vpfxt", 0, Dis_VPFXST, Int_VPFX, IN_IMM16|OUT_OTHER|IS_VFPU),
	INSTR("vpfxd", 0, Dis_VPFXD,  Int_VPFX, IN_IMM16|OUT_OTHER|IS_VFPU),
	INSTR("vpfxd", 0, Dis_VPFXD,  Int_VPFX, IN_IMM16|OUT_OTHER|IS_VFPU),
	INSTR("viim.s", 0, Dis_Viim, Int_Viim, IN_IMM16|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vfim.s", 0, Dis_Viim, Int_Viim, IN_IMM16|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
};

const MIPSInstruction tableVFPU6[32] = // 111100 xxxxx ..... . ....... . .......
{
	//0
	INSTR("vmmul", 0, Dis_MatrixMult, Int_Vmmul, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vmmul", 0, Dis_MatrixMult, Int_Vmmul, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vmmul", 0, Dis_MatrixMult, Int_Vmmul, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vmmul", 0, Dis_MatrixMult, Int_Vmmul, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),

	INSTR("v(h)tfm2", 0, Dis_Vtfm, Int_Vtfm, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("v(h)tfm2", 0, Dis_Vtfm, Int_Vtfm, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("v(h)tfm2", 0, Dis_Vtfm, Int_Vtfm, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("v(h)tfm2", 0, Dis_Vtfm, Int_Vtfm, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	//8
	INSTR("v(h)tfm3", 0, Dis_Vtfm, Int_Vtfm, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("v(h)tfm3", 0, Dis_Vtfm, Int_Vtfm, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("v(h)tfm3", 0, Dis_Vtfm, Int_Vtfm, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("v(h)tfm3", 0, Dis_Vtfm, Int_Vtfm, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),

	INSTR("v(h)tfm4", 0, Dis_Vtfm, Int_Vtfm, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("v(h)tfm4", 0, Dis_Vtfm, Int_Vtfm, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("v(h)tfm4", 0, Dis_Vtfm, Int_Vtfm, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("v(h)tfm4", 0, Dis_Vtfm, Int_Vtfm, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	//16
	INSTR("vmscl", 0, Dis_Vmscl, Int_Vmscl, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vmscl", 0, Dis_Vmscl, Int_Vmscl, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vmscl", 0, Dis_Vmscl, Int_Vmscl, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vmscl", 0, Dis_Vmscl, Int_Vmscl, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),

	INSTR("vcrsp.t/vqmul.q", 0, Dis_CrossQuat, Int_CrossQuat, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vcrsp.t/vqmul.q", 0, Dis_CrossQuat, Int_CrossQuat, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vcrsp.t/vqmul.q", 0, Dis_CrossQuat, Int_CrossQuat, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vcrsp.t/vqmul.q", 0, Dis_CrossQuat, Int_CrossQuat, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	//24
	INVALID,
	INVALID,
	INVALID,
	INVALID,
	//28
	ENCODING(VFPUMatrix1),
	INSTR("vrot", 0, Dis_VRot, Int_Vrot, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INVALID,
	INVALID,
};

// TODO: Should this only be when bit 20 is 0?
const MIPSInstruction tableVFPUMatrixSet1[16] = // 111100 11100 .xxxx . ....... . .......  (rm x is 16)
{
	INSTR("vmmov", 0, Dis_MatrixSet2, Int_Vmmov, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INVALID,
	INVALID,
	INSTR("vmidt", 0, Dis_MatrixSet1, Int_VMatrixInit, OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),

	INVALID,
	INVALID,
	INSTR("vmzero", 0, Dis_MatrixSet1, Int_VMatrixInit, OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vmone",  0, Dis_MatrixSet1, Int_VMatrixInit, OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),

	INVALID_X_8,
};

const MIPSInstruction tableVFPU9[32] = // 110100 00010 xxxxx . ....... . .......
{
	// TODO: Flags may not be correct (prefixes, etc.)
	INSTR("vsrt1", 0, Dis_Vbfy, Int_Vsrt1, IN_OTHER|OUT_OTHER|IS_VFPU),
	// TODO: Flags may not be correct (prefixes, etc.)
	INSTR("vsrt2", 0, Dis_Vbfy, Int_Vsrt2, IN_OTHER|OUT_OTHER|IS_VFPU),
	INSTR("vbfy1", 0, Dis_Vbfy, Int_Vbfy, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vbfy2", 0, Dis_Vbfy, Int_Vbfy, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	//4
	INSTR("vocp", 0, Dis_Vbfy, Int_Vocp, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),  // one's complement
	INSTR("vsocp", 0, Dis_Vbfy, Int_Vsocp, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vfad", 0, Dis_Vfad, Int_Vfad, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	// TODO: Flags may not be correct (prefixes, etc.)
	INSTR("vavg", 0, Dis_Vfad, Int_Vavg, IN_OTHER|OUT_OTHER|IS_VFPU),
	//8
	// TODO: Flags may not be correct (prefixes, etc.)
	INSTR("vsrt3", 0, Dis_Vbfy, Int_Vsrt3, IN_OTHER|OUT_OTHER|IS_VFPU),
	// TODO: Flags may not be correct (prefixes, etc.)
	INSTR("vsrt4", 0, Dis_Vbfy, Int_Vsrt4, IN_OTHER|OUT_OTHER|IS_VFPU),
	INSTR("vsgn", 0, Dis_Vbfy, Int_Vsgn, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INVALID,
	//12
	INVALID,
	INVALID,
	INVALID,
	INVALID,

	//16
	// TODO: Flags may not be correct (prefixes, etc.)
	INSTR("vmfvc", 0, Dis_Vmftvc, Int_Vmfvc, IN_OTHER|IN_VFPU_CC|OUT_OTHER|IS_VFPU),
	// TODO: Flags may not be correct (prefixes, etc.)
	INSTR("vmtvc", 0, Dis_Vmftvc, Int_Vmtvc, IN_OTHER|OUT_VFPU_CC|OUT_OTHER|IS_VFPU),
	INVALID,
	INVALID,

	//20
	INVALID, INVALID, INVALID, INVALID,
	//24
	INVALID,
	INSTR("vt4444", 0, Dis_ColorConv, Int_ColorConv, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vt5551", 0, Dis_ColorConv, Int_ColorConv, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),
	INSTR("vt5650", 0, Dis_ColorConv, Int_ColorConv, IN_OTHER|OUT_OTHER|IS_VFPU|OUT_EAT_PREFIX),

	//28
	INVALID, INVALID, INVALID, INVALID,
};

const MIPSInstruction tableALLEGREX0[32] =  // 011111 ..... ..... ..... xxxxx 100000 - or ending with 011000?
{
	INVALID,
	INVALID,
	INSTR("wsbh", 0, Dis_Allegrex2, Int_Allegrex2, IN_RT|OUT_RD),
	INSTR("wsbw", 0, Dis_Allegrex2, Int_Allegrex2, IN_RT|OUT_RD),
	INVALID, INVALID, INVALID, INVALID,
//8
	INVALID_X_8,
//16
	INSTR("seb", 0, Dis_Allegrex,Int_Allegrex, IN_RT|OUT_RD),
	INVALID,
	INVALID,
	INVALID,
//20
	INSTR("bitrev", 0, Dis_Allegrex,Int_Allegrex, IN_RT|OUT_RD),
	INVALID,
	INVALID,
	INVALID,
//24
	INSTR("seh", 0, Dis_Allegrex,Int_Allegrex, IN_RT|OUT_RD),
	INVALID,
	INVALID,
	INVALID,
//28
	INVALID,
	INVALID,
	INVALID,
	INVALID,
};


const MIPSInstruction tableEMU[4] = {
	INVALID, // removed legacy run-block opcode
	INSTR("RetKrnl", 0, Dis_Emuhack, Int_Emuhack, 0),
	INSTR("CallRepl", 0, Dis_Emuhack, Int_Emuhack, 0),
	INVALID,
};

struct EncodingBitsInfo {
	EncodingBitsInfo(u8 shift_, u8 maskBits_) : shift(shift_) {
		mask = (1 << maskBits_) - 1;
	}
	u8 shift;
	u32 mask;
};

const EncodingBitsInfo encodingBits[NumEncodings] = {
	EncodingBitsInfo(26, 6), //IMME
	EncodingBitsInfo(0,  6), //Special
	EncodingBitsInfo(0,  6), //special2
	EncodingBitsInfo(0,  6), //special3
	EncodingBitsInfo(16, 5), //RegImm
	EncodingBitsInfo(21, 5), //Cop0
	EncodingBitsInfo(0,  6), //Cop0CO
	EncodingBitsInfo(21, 5), //Cop1
	EncodingBitsInfo(16, 5), //Cop1BC
	EncodingBitsInfo(0,  6), //Cop1S
	EncodingBitsInfo(0,  6), //Cop1W
	EncodingBitsInfo(21, 5), //Cop2
	EncodingBitsInfo(16, 2), //Cop2BC2
	EncodingBitsInfo(0,  0), //Cop2Rese
	EncodingBitsInfo(23, 3), //VFPU0
	EncodingBitsInfo(23, 3), //VFPU1
	EncodingBitsInfo(23, 3), //VFPU3
	EncodingBitsInfo(21, 5), //VFPU4Jump
	EncodingBitsInfo(16, 5), //VFPU7
	EncodingBitsInfo(16, 5), //VFPU4
	EncodingBitsInfo(23, 3), //VFPU5
	EncodingBitsInfo(21, 5), //VFPU6
	EncodingBitsInfo(16, 4), //VFPUMatrix1
	EncodingBitsInfo(16, 5), //VFPU9
	EncodingBitsInfo(6,  5), //ALLEGREX0
	EncodingBitsInfo(24, 2), //EMUHACK
	EncodingBitsInfo(0,  0), //Rese
};

const MIPSInstruction *mipsTables[NumEncodings] = {
	tableImmediate,
	tableSpecial,
	tableSpecial2,
	tableSpecial3,
	tableRegImm,
	tableCop0,
	tableCop0CO,
	tableCop1,
	tableCop1BC,
	tableCop1S,
	tableCop1W,
	tableCop2,
	tableCop2BC2,
	0,
	tableVFPU0, //vfpu0
	tableVFPU1, //vfpu1
	tableVFPU3, //vfpu3
	tableVFPU4Jump,
	tableVFPU7, //vfpu4     110100 00001
	tableVFPU4, //vfpu4     110100 00000
	tableVFPU5, //vfpu5     110111
	tableVFPU6, //vfpu6     111100
	tableVFPUMatrixSet1,
	tableVFPU9,
	tableALLEGREX0,
	tableEMU,
	0,
};

//arm encoding table
//const MIPSInstruction mipsinstructions[] = 
//{
//{Comp_Unimpl,Dis_Unimpl, Info_NN,    0, 0x601,       0x1FE,0}, //could be used for drec hook :) bits 5-24 plus 0-3 are available, 19 bits are more than enough
// DATA PROCESSING INSTRUCTIONS
//                        S
//	{Comp_AND,   Dis_AND,    Info_DP,    0, DATAP(0, 0), 0x20F, {0}},
//};

// TODO : generate smart dispatcher functions from above tables
// instead of this slow method.
const MIPSInstruction *MIPSGetInstruction(MIPSOpcode op) {
	MipsEncoding encoding = Imme;
	const MIPSInstruction *instr = &tableImmediate[op.encoding >> 26];
	while (instr->altEncoding != Instruc) {
		if (instr->altEncoding == Inval) {
			//ERROR_LOG(CPU, "Invalid instruction %08x in table %i, entry %i", op, (int)encoding, subop);
			return 0; //invalid instruction
		}
		encoding = instr->altEncoding;

		const MIPSInstruction *table = mipsTables[encoding];
		const u32 subop = (op.encoding >> encodingBits[encoding].shift) & encodingBits[encoding].mask;
		instr = &table[subop];
	}
	//alright, we have a valid MIPS instruction!
	return instr;
}

void MIPSDisAsm(MIPSOpcode op, u32 pc, char *out, bool tabsToSpaces) {
	if (op == 0) {
		sprintf(out,"nop");
	} else {
		disPC = pc;
		const MIPSInstruction *instr = MIPSGetInstruction(op);
		if (instr && instr->disasm) {
			instr->disasm(op, out);
			if (tabsToSpaces) {
				while (*out) {
					if (*out == '\t')
						*out = ' ';
					out++;
				}
			}
		} else {
			strcpy(out, "no instruction :(");
		}
	}
}

#if PPSSPP_PLATFORM(WII)
// The normal and likely relative branches share a condition and completion
// path. Keep it compact and inline, avoiding a second opcode dispatch call.
// State updates match Int_RelBranch, including branches in a delay slot.
static inline void WiiInterpretRelBranch(MIPSOpcode op,unsigned kind) {
  const u32 lhs=currentMIPS->r[(op.encoding>>21)&31];
  // Equality/sign tests share the same inverse bit (bne/bgtz).
  const bool condition=(kind&2) ? int32_t(lhs)<=0 :
      lhs==currentMIPS->r[(op.encoding>>16)&31];
  const bool taken=condition != bool(kind&1);
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
// Flatten only the encoding bits used by COP1's original decoder. This is
// 192 handler pointers (768 bytes on Wii), with no guest operands or code.
static inline MIPSInterpretFunc WiiCachedFloatHandler(MIPSOpcode op) {
  struct FloatTable {
    MIPSInterpretFunc direct[32],single[64],word[64],branch[32];
    __attribute__((noinline)) FloatTable() {
      for(unsigned i=0;i<32;++i) {
        direct[i]=tableCop1[i].interpret;
        branch[i]=tableCop1BC[i].interpret;
      }
      for(unsigned i=0;i<64;++i) {
        single[i]=tableCop1S[i].interpret;
        word[i]=tableCop1W[i].interpret;
      }
    }
  };
  static const FloatTable table;
  const unsigned format=(op.encoding>>21)&31;
  if(format==16)return table.single[op.encoding&63];
  if(format==20)return table.word[op.encoding&63];
  if(format==8)return table.branch[(op.encoding>>16)&31];
  return table.direct[format];
}
static inline __attribute__((always_inline)) bool WiiInterpretFast(MIPSOpcode op) {
  const unsigned kind=op.encoding>>26;
  switch(kind) {
  case 2:case 3:
    MIPSInt::Int_JumpType(op);return true;
  case 4:case 5:case 6:case 7:
  case 20:case 21:case 22:case 23:
    WiiInterpretRelBranch(op,kind);return true;
  case 1:
    if((((op.encoding>>16)&31)&~19u)==0) {
      MIPSInt::Int_RelBranchRI(op);return true;
    }
    break;
  case 0:
    // The all-zero instruction has no effects except advancing PC. Keep its
    // boundary visible; branch-delay completion remains in the caller.
    if(op.encoding==0) {currentMIPS->pc+=4;return true;}
    if((op.encoding&63)==8||(op.encoding&63)==9) {
      MIPSInt::Int_JumpRegType(op);return true;
    }
    if(Wii::InterpretScalar(op.encoding,currentMIPS->r,currentMIPS->pc))return true;
    if((op.encoding&63)==12) {
      MIPSInt::Int_Syscall(op);return true;
    }
    return Wii::InterpretHiLo(op.encoding,currentMIPS->r,currentMIPS->lo,currentMIPS->hi,currentMIPS->pc);
  case 9:case 10:case 11:case 12:case 13:case 14:case 15:case 31:
    return Wii::InterpretScalar(op.encoding,currentMIPS->r,currentMIPS->pc);
  default:
    if(kind>=32)return Wii::InterpretMemory(op.encoding,currentMIPS->r,currentMIPS->pc);
    break;
  }
  // Unsupported encodings and remaining coprocessors retain decoding.
  return false;
}
#endif
#if PPSSPP_PLATFORM(WII)
// Flatten the deeper decoder paths once. Operand registers/immediates remain
// live inputs to the original handlers; these tables contain no guest code.
static inline __attribute__((always_inline)) MIPSInterpretFunc WiiCachedHandler(MIPSOpcode op) {
  const unsigned kind=op.encoding>>26;
  if(kind==17)return WiiCachedFloatHandler(op);
  if(kind==52||kind==60) {
    struct VectorTable {
      // Only four subtables use bits 16..20. The other selectors ignore
      // those operand bits, so do not repeat each handler 32 times.
      MIPSInterpretFunc direct[2][32],unary[3][32],matrix[16];
      __attribute__((noinline)) VectorTable() {
        for(unsigned i=0;i<32;++i) {
          direct[0][i]=tableVFPU4Jump[i].interpret;
          direct[1][i]=tableVFPU6[i].interpret;
          unary[0][i]=tableVFPU4[i].interpret;
          unary[1][i]=tableVFPU7[i].interpret;
          unary[2][i]=tableVFPU9[i].interpret;
        }
        for(unsigned i=0;i<16;++i)
          matrix[i]=tableVFPUMatrixSet1[i].interpret;
      }
    };
    static const VectorTable table;
    const unsigned group=(op.encoding>>21)&31;
    if(kind==52&&group<3)return table.unary[group][(op.encoding>>16)&31];
    if(kind==60&&group==28)return table.matrix[(op.encoding>>16)&15];
    return table.direct[kind==60][group];
  }
  return MIPSGetInterpretFunc(op);
}
#if defined(PPSSPP_WII_PORTABLE_TEST)
MIPSInterpretFunc MIPSTestCachedHandler(MIPSOpcode op) {return WiiCachedHandler(op);}
#endif
#endif
#if PPSSPP_PLATFORM(WII)
__attribute__((noinline))
#endif
static void MIPSInterpretSlow(MIPSOpcode op) {
#if PPSSPP_PLATFORM(WII)
  // Keep COP1 memory/conversion state out of the integer hot dispatcher.
  // Unsupported or unaligned addresses still use the original memory handler.
  const unsigned kind=op.encoding>>26;
  if((kind==49||kind==57)&&Wii::InterpretFPMemory(op.encoding,currentMIPS->r,currentMIPS->fi,currentMIPS->pc))return;
  auto handler=WiiCachedHandler(op);
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

void MIPSInterpret(MIPSOpcode op) {
#if PPSSPP_PLATFORM(WII)
  Wii::TraceInstruction(currentMIPS->pc,op.encoding,currentMIPS->r[29],currentMIPS->r[31]);
  if(WiiInterpretFast(op))return;
#endif
  MIPSInterpretSlow(op);
}

#define _RS   ((op>>21) & 0x1F)
#define _RT   ((op>>16) & 0x1F)
#define _RD   ((op>>11) & 0x1F)
#define R(i)   (curMips->r[i])


int MIPSInterpret_RunUntil(u64 globalTicks)
{
	MIPSState *curMips = currentMIPS;
#if PPSSPP_PLATFORM(WII)
	Wii::InstructionFetch fetch;
  unsigned hostCheck=0;
  const unsigned diagnosticLevel=Wii::DiagnosticLevel();
  const bool traceInstructions=diagnosticLevel>=2;
#endif
	while (coreState == CORE_RUNNING)
	{
    {Wii::CpuScope cpuTiming(Wii::CpuPhase::Timing);
		CoreTiming::Advance();}
		u32 lastPC = 0;

		// NEVER stop in a delay slot!
		while (curMips->downcount >= 0 && coreState == CORE_RUNNING)
		{
#if PPSSPP_PLATFORM(WII)
      if(++hostCheck>=4096) {
        hostCheck=0;
        Wii::RuntimeProgress(curMips->pc,1);
        if(Wii::EmulationStopRequested())return 1;
      }
#endif



			// int cycles = 0;
			{
				again:
#if PPSSPP_PLATFORM(WII)
				MIPSOpcode op = MIPSOpcode(fetch.Read(curMips->pc));
#else
				MIPSOpcode op = MIPSOpcode(Memory::Read_U32(curMips->pc));
#endif
				//MIPSOpcode op = Memory::ReadOpcode(mipsr4k.pc);
				/*
				// Choke on VFPU
				MIPSInfo info = MIPSGetInfo(op);
				if (info & IS_VFPU)
				{
					if (!Core_IsStepping() && !GetAsyncKeyState(VK_LSHIFT))
					{
						Core_EnableStepping(true);
						return;
					}
				}*/

		//2: check for breakpoint (VERY SLOW)
#if defined(_DEBUG)
				if (CBreakPoints::IsAddressBreakPoint(curMips->pc))
				{
					auto cond = CBreakPoints::GetBreakPointCondition(currentMIPS->pc);
					if (!cond || cond->Evaluate())
					{
						Core_EnableStepping(true);
						if (CBreakPoints::IsTempBreakPoint(curMips->pc))
							CBreakPoints::RemoveBreakPoint(curMips->pc);
						break;
					}
				}
#endif

#if PPSSPP_PLATFORM(WII)
                if(traceInstructions)Wii::TraceInstruction(curMips->pc,op.encoding,curMips->r[29],curMips->r[31]);
#endif
				bool wasInDelaySlot = curMips->inDelaySlot;

				/*
				if (curMips->pc != lastPC + 4) {
					if (blockCount > 0) {
						MIPSState *mips_ = curMips;
						fprintf(f, "BLOCK : %08x v0: %08x v1: %08x a0: %08x s0: %08x s4: %08x\n", mips_->pc, mips_->r[MIPS_REG_V0], mips_->r[MIPS_REG_V1], mips_->r[MIPS_REG_A0], mips_->r[MIPS_REG_S0], mips_->r[MIPS_REG_S4]);
						fflush(f);
						blockCount--;
					}
				}
				lastPC = curMips->pc;
				*/
#if PPSSPP_PLATFORM(WII)
                {
                  Wii::CpuScope cpuInterpreter(Wii::CpuPhase::Interpreter,true,op.encoding);
                  Wii::CpuProfileCount(wasInDelaySlot?6:5);
                  bool folded=false;
#if !defined(_DEBUG) && !defined(WII_DISABLE_CONSTANT_PAIRS)
                  // Do not fuse across an event, tick limit, branch delay slot,
                  // debugger boundary or trace boundary. Fetch both live words.
                  if((op.encoding&0xffe00000u)==0x3c000000u&&
                     !wasInDelaySlot&&!traceInstructions&&curMips->downcount>=1&&
                     CoreTiming::GetTicksFromDowncount(curMips->downcount-1)<=globalTicks) {
                    u32 second;
                    folded=fetch.ReadCommand(curMips->pc+4,second)&&Wii::FoldConstantPair(op.encoding,second,curMips->r,curMips->pc);
                    if(folded) {--curMips->downcount;++hostCheck;Wii::CpuProfileCount(5);}
                  }
#endif
                  if(!folded&&!WiiInterpretFast(op))MIPSInterpretSlow(op);
                }
#else
                MIPSInterpret(op);
#endif

				if (curMips->inDelaySlot)
				{
					// The reason we have to check this is the delay slot hack in Int_Syscall.
					if (wasInDelaySlot)
					{
						curMips->pc = curMips->nextPC;
						curMips->inDelaySlot = false;
					}
					if (curMips->inDelaySlot) {
						curMips->downcount -= 1;
						goto again;
					}
				}
			}

			curMips->downcount -= 1;
#if PPSSPP_PLATFORM(WII)
            if (CoreTiming::GetTicksFromDowncount(currentMIPS->downcount) > globalTicks)
#else
			if (CoreTiming::GetTicks() > globalTicks)
#endif
			{
				// DEBUG_LOG(CPU, "Hit the max ticks, bailing 1 : %llu, %llu", globalTicks, CoreTiming::GetTicks());
				return 1;
			}
		}
	}

	return 1;
}

const char *MIPSGetName(MIPSOpcode op)
{
	static const char *noname = "unk";
	const MIPSInstruction *instr = MIPSGetInstruction(op);
	if (!instr)
		return noname;
	else
		return instr->name;
}

MIPSInfo MIPSGetInfo(MIPSOpcode op)
{
	//	int crunch = CRUNCH_MIPS_OP(op);
	const MIPSInstruction *instr = MIPSGetInstruction(op);
	if (instr)
		return instr->flags;
	else
		return MIPSInfo(BAD_INSTRUCTION);
}

MIPSInterpretFunc MIPSGetInterpretFunc(MIPSOpcode op)
{
	const MIPSInstruction *instr = MIPSGetInstruction(op);
	if (instr && instr->interpret)
		return instr->interpret;
	else
		return 0;
}

// TODO: Do something that makes sense here.
int MIPSGetInstructionCycleEstimate(MIPSOpcode op)
{
	MIPSInfo info = MIPSGetInfo(op);
	if (info & DELAYSLOT)
		return 2;
	else
		return 1;
}

const char *MIPSDisasmAt(u32 compilerPC) {
	static char temp[256];
	MIPSDisAsm(Memory::Read_Instruction(compilerPC), 0, temp);
	return temp;
}
