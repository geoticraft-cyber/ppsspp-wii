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

#include "zlib.h"
#include <stdio.h>
#include <cstdio>

#include "Core/MemMap.h"
#include "Core/System.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/HLE/scePauth.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Common/FileUtil.h"

static int scePauthCommon(const char *nameForLog, u32 srcPtr, int srcLength, u32 destLengthPtr, u32 workArea)
{
	INFO_LOG(HLE, "%s(%08x, %08x, %08x, %08x)", nameForLog, srcPtr, srcLength, destLengthPtr, workArea);

	// RECOVERED_1_2 from target fn around 0x800c6ddc: reject malformed guest
	// buffers before hashing/reading and initialize output length to zero.
	if (srcLength <= 0 || !Memory::IsValidRange(srcPtr, (u32)srcLength) ||
		!Memory::IsValidRange(workArea, 16) || !Memory::IsValidRange(destLengthPtr, 4)) {
		return -1;
	}
	Memory::Write_U32(0, destLengthPtr);

	u8 *src = (u8 *)Memory::GetPointer(srcPtr);
	const u32 crc = crc32(0, src, srcLength);
	char name[256];
	std::string hostPath;
	std::sprintf(name, "ms0:/PAUTH");
	pspFileSystem.GetHostPath(std::string(name), hostPath);
	std::sprintf(name, "%s/pauth_%08x.bin.decrypt", hostPath.c_str(), crc);
	FILE *fp = File::OpenCFile(name, "rb");
	if (fp) {
		std::fseek(fp, 0, SEEK_END);
		long fileSize = std::ftell(fp);
		std::fseek(fp, 0, SEEK_SET);
		if (fileSize >= 0 && fileSize <= srcLength &&
			std::fread(src, 1, (size_t)fileSize, fp) == (size_t)fileSize) {
			std::fclose(fp);
			Memory::Write_U32((u32)fileSize, destLengthPtr);
			INFO_LOG(HLE, "Read from decrypted file %s", name);
			return 0;
		}
		std::fclose(fp);
	}

	// Binary-proven 1.2 semantic change: do not dump encrypted payloads/keys and
	// do not fake decryption success for F7AA47F6.
	ERROR_LOG(HLE, "PAUTH: no decrypted data; returning error instead of dumping files or claiming decryption");
	return -1;
}

static int scePauth_F7AA47F6(u32 srcPtr, int srcLength, u32 destLengthPtr, u32 workArea)
{
	return scePauthCommon("scePauth_F7AA47F6", srcPtr, srcLength, destLengthPtr, workArea);
}

static int scePauth_98B83B5D(u32 srcPtr, int srcLength, u32 destLengthPtr, u32 workArea)
{
	return scePauthCommon("scePauth_98B83B5D", srcPtr, srcLength, destLengthPtr, workArea);
}

const HLEFunction scePauth[] = {
	{0XF7AA47F6, &WrapI_UIUU<scePauth_F7AA47F6>,     "scePauth_F7AA47F6", 'i', "xixx"},
	{0X98B83B5D, &WrapI_UIUU<scePauth_98B83B5D>,     "scePauth_98B83B5D", 'i', "xixx"},
};

void Register_scePauth()
{
	RegisterModule("scePauth", ARRAY_SIZE(scePauth), scePauth);
}
