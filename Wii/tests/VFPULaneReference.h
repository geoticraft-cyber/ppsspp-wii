// SPDX-License-Identifier: GPL-2.0-or-later
// Frozen vector helper bodies from the preceding compact CPU build.
__attribute__((noinline)) void PreviousReadVector(float *rd, VectorSize size, int reg) {
	int row = 0;
	int length = 0;

	switch (size) {
	case V_Single: rd[0] = V(reg); return; // transpose = 0; row=(reg>>5)&3; length = 1; break;
	case V_Pair:   row=(reg>>5)&2; length = 2; break;
	case V_Triple: row=(reg>>6)&1; length = 3; break;
	case V_Quad:   row=(reg>>5)&2; length = 4; break;
	default: _assert_msg_(CPU, 0, "%s: Bad vector size", __FUNCTION__);
	}
	int transpose = (reg>>5) & 1;
	const int mtx = (reg >> 2) & 7;
	const int col = reg & 3;

	if (transpose) {
		const int base = mtx * 4 + col * 32;
		for (int i = 0; i < length; i++)
			rd[i] = V(base + ((row+i)&3));
	} else {
		const int base = mtx * 4 + col;
		for (int i = 0; i < length; i++)
			rd[i] = V(base + ((row+i)&3)*32);
	}
}
__attribute__((noinline)) void PreviousWriteVector(const float *rd, VectorSize size, int reg) {
	int row = 0;
	int length = 0;

	switch (size) {
	case V_Single: V(reg) = rd[0]; return; // transpose = 0; row=(reg>>5)&3; length = 1; break;
	case V_Pair:   row=(reg>>5)&2; length = 2; break;
	case V_Triple: row=(reg>>6)&1; length = 3; break;
	case V_Quad:   row=(reg>>5)&2; length = 4; break;
	default: _assert_msg_(CPU, 0, "%s: Bad vector size", __FUNCTION__);
	}
	const int mtx = (reg>>2)&7;
	const int col = reg & 3;
	int transpose = (reg>>5)&1;

	if (currentMIPS->VfpuWriteMask() == 0) {
		if (transpose) {
			const int base = mtx * 4 + col * 32;
			for (int i = 0; i < length; i++)
				V(base + ((row+i)&3)) = rd[i];
		} else {
			const int base = mtx * 4 + col;
			for (int i = 0; i < length; i++)
				V(base + ((row+i)&3)*32) = rd[i];
		}
	} else {
		for (int i = 0; i < length; i++) {
			if (!currentMIPS->VfpuWriteMask(i)) {
				int index = mtx * 4;
				if (transpose)
					index += ((row+i)&3) + col*32;
				else
					index += col + ((row+i)&3)*32;
				V(index) = rd[i];
			}
		}
	}
}
