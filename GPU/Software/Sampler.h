// Copyright (c) 2017- PPSSPP Project.

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

#pragma once

#include "ppsspp_config.h"

#include "GPU/Math3D.h"

namespace Sampler {

typedef u32 (*NearestFunc)(int u, int v, const u8 *tptr, int bufw, int level);
NearestFunc GetNearestFunc();

typedef u32 (*LinearFunc)(int u[4], int v[4], int frac_u, int frac_v, const u8 *tptr, int bufw, int level);
LinearFunc GetLinearFunc();

struct Funcs {
	NearestFunc nearest;
	LinearFunc linear;
};
static inline Funcs GetFuncs() {
	Funcs f;
	f.nearest = GetNearestFunc();
	f.linear = GetLinearFunc();
	return f;
}

void Init();
void Shutdown();


};
