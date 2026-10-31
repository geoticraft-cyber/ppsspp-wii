// Copyright (c) 2014- PPSSPP Project.

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

#include "Common/ChunkFile.h"
#include "Core/MemMapHelpers.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/HLEHelperThread.h"
#include "Core/CoreTiming.h"
#include "Core/HLE/KernelWaitHelpers.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/MIPS/MIPSCodeUtils.h"

HLEHelperThread::HLEHelperThread() : id_(-1), entry_(0) {
}

HLEHelperThread::HLEHelperThread(const char *threadName, u32 instructions[], u32 instrCount, u32 prio, int stacksize) : id_(-1), entry_(0) {
	u32 instrBytes = instrCount * sizeof(u32);
	u32 totalBytes = instrBytes + sizeof(u32) * 2;
	AllocEntry(totalBytes);
	if (!entry_) return;
	// Host instructions are numeric words; PSP code memory is little endian.
	for (u32 i = 0; i < instrCount; ++i)
		Memory::Write_U32(instructions[i], entry_ + i * sizeof(u32));

	// Just to simplify things, we add the return here.
	Memory::Write_U32(MIPS_MAKE_JR_RA(), entry_ + instrBytes + 0);
	Memory::Write_U32(MIPS_MAKE_NOP(), entry_ + instrBytes + 4);

	Create(threadName, prio, stacksize);
}

HLEHelperThread::HLEHelperThread(const char *threadName, const char *module, const char *func, u32 prio, int stacksize) : id_(-1), entry_(0) {
	const u32 bytes = sizeof(u32) * 2;
	AllocEntry(bytes);
	if (!entry_) return;
	Memory::Write_U32(MIPS_MAKE_JR_RA(), entry_ + 0);
	Memory::Write_U32(MIPS_MAKE_SYSCALL(module, func), entry_ + 4);

	Create(threadName, prio, stacksize);
}

HLEHelperThread::~HLEHelperThread() {
	if (id_ > 0)
		__KernelDeleteThread(id_, SCE_KERNEL_ERROR_THREAD_TERMINATED, "helper deleted");
	if (entry_)
		kernelMemory.Free(entry_);
}

void HLEHelperThread::AllocEntry(u32 size) {
	entry_ = kernelMemory.Alloc(size);
	if (entry_ == (u32)-1) { entry_ = 0; return; }
	currentMIPS->InvalidateICache(entry_, size);
}

void HLEHelperThread::Create(const char *threadName, u32 prio, int stacksize) {
	id_ = __KernelCreateThreadInternal(threadName, __KernelGetCurThreadModuleId(), entry_, prio, stacksize, 0);
}

void HLEHelperThread::DoState(PointerWrap &p) {
	auto s = p.Section("HLEHelperThread", 1);
	if (!s) {
		return;
	}

	p.Do(id_);
	p.Do(entry_);
}

void HLEHelperThread::Start(u32 a0, u32 a1) {
	if (Valid()) __KernelStartThread(id_, a0, a1, true);
}

void HLEHelperThread::Terminate() {
	__KernelStopThread(id_, SCE_KERNEL_ERROR_THREAD_TERMINATED, "helper terminated");
}

// This core predates KernelIsThreadDormant/KernelChangeThreadPriority.
bool HLEHelperThread::Stopped() {
	return id_ <= 0 || !kernelObjects.IsValid(id_) ||
		sceKernelGetThreadExitStatus(id_) != (int)SCE_KERNEL_ERROR_NOT_DORMANT;
}
void HLEHelperThread::ChangePriority(u32 priority) {
	sceKernelChangeThreadPriority(id_, (int)priority);
}
void HLEHelperThread::Resume(WaitType waitType, SceUID uid, int result) {
	HLEKernel::ResumeFromWait(id_, waitType, uid, result);
}
