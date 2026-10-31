// SPDX-License-Identifier: GPL-2.0-or-later
void PreviousDecoder::ComputeSkinMatrix(const float weights[8]) const {
	memset(skinMatrix, 0, sizeof(skinMatrix));
	for (int j = 0; j < nweights; j++) {
		const float *bone = &gstate.boneMatrix[j * 12];
		if (weights[j] != 0.0f) {
			for (int i = 0; i < 12; i++) {
				skinMatrix[i] += weights[j] * bone[i];
			}
		}
	}
}
