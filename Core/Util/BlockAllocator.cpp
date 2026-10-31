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

#include <cstring>

#include "Common/Log.h"
#include "Common/ChunkFile.h"
#include "Common/StringUtils.h"
#include "Core/Util/BlockAllocator.h"
#include "Core/Reporting.h"
#if PPSSPP_PLATFORM(WII)
#include "Wii/AllocationDiagnostics.h"
#endif

u32 BlockAllocator::AllocationFailed(const char *kind, u32 requested, u32 effective, u32 alignment, u32 address) const {
#if PPSSPP_PLATFORM(WII)
	Wii::AllocationFailure(kind, requested, effective, alignment,
		reinterpret_cast<uintptr_t>(__builtin_return_address(0)), 0, address,
		GetTotalFreeBytes(), GetLargestFreeBlockSize());
#else
	ERROR_LOG(SCEKERNEL, "%s requested=%u effective=%u alignment=%u address=%08x free=%u largest=%u",
		kind, requested, effective, alignment, address, GetTotalFreeBytes(), GetLargestFreeBlockSize());
#endif
	return u32(-1);
}

// Slow freaking thing but works (eventually) :)

BlockAllocator::BlockAllocator(int grain) : bottom_(NULL), top_(NULL), grain_(grain > 0 && (grain & (grain - 1)) == 0 ? grain : 16)
{
}

BlockAllocator::~BlockAllocator()
{
	Shutdown();
}

void BlockAllocator::Init(u32 rangeStart, u32 rangeSize)
{
	Shutdown();
	if (!rangeSize || uint64_t(rangeStart) + rangeSize > UINT32_MAX) {
		ERROR_LOG(SCEKERNEL,"Invalid block allocator region: %08x size=%08x",rangeStart,rangeSize);
		return;
	}
	rangeStart_ = rangeStart;
	rangeSize_ = rangeSize;
	//Initial block, covering everything
	top_ = new Block(rangeStart_, rangeSize_, false, NULL, NULL);
	bottom_ = top_;
}

void BlockAllocator::Shutdown()
{
	while (bottom_ != NULL)
	{
		Block *next = bottom_->next;
		delete bottom_;
		bottom_ = next;
	}
	top_ = NULL;
	rangeStart_ = rangeSize_ = 0;
}

u32 BlockAllocator::AllocAligned(u32 &size, u32 sizeGrain, u32 grain, bool fromTop, const char *tag)
{
	const u32 requested=size;
	// Sanity check
	if (size == 0 || size > rangeSize_) {
		const u32 failure = AllocationFailed("guest-aligned",requested,size,grain,rangeStart_);
		ERROR_LOG(SCEKERNEL, "Clearly bogus size: %08x - failing allocation", size);
		return failure;
	}

	// It could be off step, but the grain should generally be a power of 2.
	if (grain < grain_)
		grain = grain_;
	if (sizeGrain < grain_)
		sizeGrain = grain_;

	// Wide rounding prevents a large size grain from wrapping into a tiny block.
	const uint64_t rounded = (uint64_t(size) + sizeGrain - 1) & ~(uint64_t(sizeGrain) - 1);
	if ((sizeGrain & (sizeGrain - 1)) || !rounded || rounded > rangeSize_)
		return AllocationFailed("guest-aligned",requested,rounded > UINT32_MAX ? UINT32_MAX : u32(rounded),grain,rangeStart_);
	size = u32(rounded);

	if (!fromTop)
	{
		//Allocate from bottom of mem
		for (Block *bp = bottom_; bp != NULL; bp = bp->next)
		{
			Block &b = *bp;
			u32 offset = b.start % grain;
			if (offset != 0)
				offset = grain - offset;
			uint64_t needed = uint64_t(offset) + size;
			if (b.taken == false && b.size >= needed)
			{
				if (b.size == needed)
				{
					if (offset >= grain_)
						InsertFreeBefore(&b, offset);
					b.taken = true;
					b.SetTag(tag);
					return b.start;
				}
				else
				{
					InsertFreeAfter(&b, b.size - needed);
					if (offset >= grain_)
						InsertFreeBefore(&b, offset);
					b.taken = true;
					b.SetTag(tag);
					return b.start;
				}
			}
		}
	}
	else
	{
		// Allocate from top of mem.
		for (Block *bp = top_; bp != NULL; bp = bp->prev)
		{
			Block &b = *bp;
			if (b.taken || b.size < size) continue;
			u32 offset = (uint64_t(b.start) + b.size - size) % grain;
			uint64_t needed = uint64_t(offset) + size;
			if (b.taken == false && b.size >= needed)
			{
				if (b.size == needed)
				{
					if (offset >= grain_)
						InsertFreeAfter(&b, offset);
					b.taken = true;
					b.SetTag(tag);
					return b.start;
				}
				else
				{
					InsertFreeBefore(&b, b.size - needed);
					if (offset >= grain_)
						InsertFreeAfter(&b, offset);
					b.taken = true;
					b.SetTag(tag);
					return b.start;
				}
			}
		}
	}

	//Out of memory :(
	const u32 failure = AllocationFailed("guest-aligned",requested,size,grain,rangeStart_);
	ListBlocks();
	ERROR_LOG(SCEKERNEL, "Block Allocator (%08x-%08x) failed to allocate %i (%08x) bytes of contiguous memory", rangeStart_, rangeStart_ + rangeSize_, size, size);
	return failure;
}

u32 BlockAllocator::Alloc(u32 &size, bool fromTop, const char *tag)
{
	// We want to make sure it's aligned in case AllocAt() was used.
	return AllocAligned(size, grain_, grain_, fromTop, tag);
}

u32 BlockAllocator::AllocAt(u32 position, u32 size, const char *tag)
{
	const u32 requested=size;
	const u32 alignedPosition=position & ~(grain_ - 1);
	const uint64_t rounded=(uint64_t(position - alignedPosition) + size + grain_ - 1) & ~(uint64_t(grain_) - 1);
	const u32 effective=rounded > UINT32_MAX ? UINT32_MAX : u32(rounded);
	if (!size || !rangeSize_ || alignedPosition < rangeStart_ || rounded > UINT32_MAX ||
		uint64_t(alignedPosition)+rounded > uint64_t(rangeStart_)+rangeSize_)
		return AllocationFailed("guest-at",requested,effective,grain_,position);

	Block *b=GetBlockFromAddress(alignedPosition);
	if (!b || b->taken || uint64_t(alignedPosition)+rounded > uint64_t(b->start)+b->size)
		return AllocationFailed("guest-at",requested,effective,grain_,position);

	if (b->start != alignedPosition) InsertFreeBefore(b,alignedPosition-b->start);
	if (b->size != effective) InsertFreeAfter(b,b->size-effective);
	b->taken=true;
	b->SetTag(tag);
	return position;
}

void BlockAllocator::MergeFreeBlocks(Block *fromBlock)
{
	DEBUG_LOG(SCEKERNEL, "Merging Blocks");

	Block *prev = fromBlock->prev;
	while (prev != NULL && prev->taken == false)
	{
		DEBUG_LOG(SCEKERNEL, "Block Alloc found adjacent free blocks - merging");
		prev->size += fromBlock->size;
		if (fromBlock->next == NULL)
			top_ = prev;
		else
			fromBlock->next->prev = prev;
		prev->next = fromBlock->next;
		delete fromBlock;
		fromBlock = prev;
		prev = fromBlock->prev;
	}

	if (prev == NULL)
		bottom_ = fromBlock;
	else
		prev->next = fromBlock;

	Block *next = fromBlock->next;
	while (next != NULL && next->taken == false)
	{
		DEBUG_LOG(SCEKERNEL, "Block Alloc found adjacent free blocks - merging");
		fromBlock->size += next->size;
		fromBlock->next = next->next;
		delete next;
		next = fromBlock->next;
	}

	if (next == NULL)
		top_ = fromBlock;
	else
		next->prev = fromBlock;
}

bool BlockAllocator::Free(u32 position)
{
	Block *b = GetBlockFromAddress(position);
	if (b && b->taken)
	{
		b->taken = false;
		MergeFreeBlocks(b);
		return true;
	}
	else
	{
		ERROR_LOG(SCEKERNEL, "BlockAllocator : invalid free %08x", position);
		return false;
	}
}

bool BlockAllocator::FreeExact(u32 position)
{
	Block *b = GetBlockFromAddress(position);
	if (b && b->taken && b->start == position)
	{
		b->taken = false;
		MergeFreeBlocks(b);
		return true;
	}
	else
	{
		ERROR_LOG(SCEKERNEL, "BlockAllocator : invalid free %08x", position);
		return false;
	}
}

BlockAllocator::Block *BlockAllocator::InsertFreeBefore(Block *b, u32 size)
{
	Block *inserted = new Block(b->start, size, false, b->prev, b);
	b->prev = inserted;
	if (inserted->prev == NULL)
		bottom_ = inserted;
	else
		inserted->prev->next = inserted;

	b->start += size;
	b->size -= size;
	return inserted;
}

BlockAllocator::Block *BlockAllocator::InsertFreeAfter(Block *b, u32 size)
{
	Block *inserted = new Block(b->start + b->size - size, size, false, b, b->next);
	b->next = inserted;
	if (inserted->next == NULL)
		top_ = inserted;
	else
		inserted->next->prev = inserted;

	b->size -= size;
	return inserted;
}

void BlockAllocator::CheckBlocks() const
{
	for (const Block *bp = bottom_; bp != NULL; bp = bp->next)
	{
		const Block &b = *bp;
		if (b.start > 0xc0000000) {  // probably free'd debug values
			ERROR_LOG_REPORT(HLE, "Bogus block in allocator");
		}
		// Outside the valid range, probably logic bug in allocation.
		if (b.start + b.size > rangeStart_ + rangeSize_ || b.start < rangeStart_) {
			ERROR_LOG_REPORT(HLE, "Bogus block in allocator");
		}
	}
}

const char *BlockAllocator::GetBlockTag(u32 addr) const {
	const Block *b = GetBlockFromAddress(addr);
	return b->tag;
}

inline BlockAllocator::Block *BlockAllocator::GetBlockFromAddress(u32 addr)
{
	for (Block *bp = bottom_; bp != NULL; bp = bp->next)
	{
		Block &b = *bp;
		if (b.start <= addr && b.start + b.size > addr)
		{
			// Got one!
			return bp;
		}
	}
	return NULL;
}

const BlockAllocator::Block *BlockAllocator::GetBlockFromAddress(u32 addr) const
{
	for (const Block *bp = bottom_; bp != NULL; bp = bp->next)
	{
		const Block &b = *bp;
		if (b.start <= addr && b.start + b.size > addr)
		{
			// Got one!
			return bp;
		}
	}
	return NULL;
}

u32 BlockAllocator::GetBlockStartFromAddress(u32 addr) const
{
	const Block *b = GetBlockFromAddress(addr);
	if (b)
		return b->start;
	else
		return -1;
}

u32 BlockAllocator::GetBlockSizeFromAddress(u32 addr) const
{
	const Block *b = GetBlockFromAddress(addr);
	if (b)
		return b->size;
	else
		return -1;
}

void BlockAllocator::ListBlocks() const
{
	INFO_LOG(SCEKERNEL,"-----------");
	for (const Block *bp = bottom_; bp != NULL; bp = bp->next)
	{
		const Block &b = *bp;
		INFO_LOG(SCEKERNEL, "Block: %08x - %08x size %08x taken=%i tag=%s", b.start, b.start+b.size, b.size, b.taken ? 1:0, b.tag);
	}
	INFO_LOG(SCEKERNEL,"-----------");
}

u32 BlockAllocator::GetLargestFreeBlockSize() const
{
	u32 maxFreeBlock = 0;
	for (const Block *bp = bottom_; bp != NULL; bp = bp->next)
	{
		const Block &b = *bp;
		if (!b.taken)
		{
			if (b.size > maxFreeBlock)
				maxFreeBlock = b.size;
		}
	}
	if (maxFreeBlock & (grain_ - 1))
		WARN_LOG_REPORT(HLE, "GetLargestFreeBlockSize: free size %08x does not align to grain %08x.", maxFreeBlock, grain_);
	return maxFreeBlock;
}

u32 BlockAllocator::GetTotalFreeBytes() const
{
	u32 sum = 0;
	for (const Block *bp = bottom_; bp != NULL; bp = bp->next)
	{
		const Block &b = *bp;
		if (!b.taken)
		{
			sum += b.size;
		}
	}
	if (sum & (grain_ - 1))
		WARN_LOG_REPORT(HLE, "GetTotalFreeBytes: free size %08x does not align to grain %08x.", sum, grain_);
	return sum;
}

void BlockAllocator::DoState(PointerWrap &p)
{
	auto s = p.Section("BlockAllocator", 1);
	if (!s)
		return;

	int count = 0;

	if (p.mode == p.MODE_READ)
	{
		Shutdown();
		p.Do(count);

		bottom_ = new Block(0, 0, false, NULL, NULL);
		bottom_->DoState(p);
		--count;

		top_ = bottom_;
		for (int i = 0; i < count; ++i)
		{
			top_->next = new Block(0, 0, false, top_, NULL);
			top_->next->DoState(p);
			top_ = top_->next;
		}
	}
	else
	{
		for (const Block *bp = bottom_; bp != NULL; bp = bp->next)
			++count;
		p.Do(count);

		bottom_->DoState(p);
		--count;

		Block *last = bottom_;
		for (int i = 0; i < count; ++i)
		{
			last->next->DoState(p);
			last = last->next;
		}
	}

	p.Do(rangeStart_);
	p.Do(rangeSize_);
	p.Do(grain_);
}

BlockAllocator::Block::Block(u32 _start, u32 _size, bool _taken, Block *_prev, Block *_next)
: start(_start), size(_size), taken(_taken), prev(_prev), next(_next)
{
	truncate_cpy(tag, "(untitled)");
}

void BlockAllocator::Block::SetTag(const char *_tag)
{
	if (_tag)
		truncate_cpy(tag, _tag);
	else
		truncate_cpy(tag, "---");
}

void BlockAllocator::Block::DoState(PointerWrap &p)
{
	auto s = p.Section("Block", 1);
	if (!s)
		return;

	p.Do(start);
	p.Do(size);
	p.Do(taken);
	p.DoArray(tag, sizeof(tag));
}
