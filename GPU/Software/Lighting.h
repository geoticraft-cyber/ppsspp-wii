// Copyright (c) 2013- PPSSPP Project.

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

#include "TransformUnit.h"

namespace Lighting {

void Process(VertexData& vertex, bool hasColor);

#if PPSSPP_PLATFORM(WII)
// Immutable for one primitive submission. Source registers are re-read for
// every draw, so light/material changes cannot reuse stale derived values.
struct PreparedLight {
  Vec3<float> position,unitPosition,unitDirection,unitHalf,ambient,diffuse,specular;
  float constantAtt,linearAtt,quadraticAtt,cutoff,exponent;
  bool enabled,directional,spot,powered,useSpecular;
};
struct PreparedState {
  PreparedLight lights[4];
  unsigned activeLights[4],activeCount;
  Vec3<float> emissive,ambient,diffuse,specular,globalAmbient;
  float specularCoef;
  unsigned materialUpdate,ambientAlpha,materialAlpha,uvLight0,uvLight1;
  bool environment,enabled,secondary;
};
void Prepare(PreparedState &state);
void ProcessPrepared(VertexData &vertex,bool hasColor,const PreparedState &state);
#endif

}
