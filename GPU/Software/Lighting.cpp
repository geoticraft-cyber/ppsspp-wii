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

#include "../GPUState.h"

#include "Lighting.h"

namespace Lighting {

void Process(VertexData& vertex, bool hasColor)
{
	const int materialupdate = gstate.materialupdate & (hasColor ? 7 : 0);

	Vec3<float> vcol0 = vertex.color0.rgb().Cast<float>() * Vec3<float>::AssignToAll(1.0f / 255.0f);
	Vec3<float> mec = Vec3<float>::FromRGB(gstate.getMaterialEmissive());

	Vec3<float> mac = (materialupdate & 1) ? vcol0 : Vec3<float>::FromRGB(gstate.getMaterialAmbientRGBA());
	Vec3<float> final_color = mec + mac * Vec3<float>::FromRGB(gstate.getAmbientRGBA());
	Vec3<float> specular_color(0.0f, 0.0f, 0.0f);

	for (unsigned int light = 0; light < 4; ++light) {
		// Always calculate texture coords from lighting results if environment mapping is active
		// TODO: Should specular lighting should affect this, too?  Doesn't in GLES.
		// TODO: Not sure if this really should be done even if lighting is disabled altogether
		if (gstate.getUVGenMode() == GE_TEXMAP_ENVIRONMENT_MAP) {
			Vec3<float> L = Vec3<float>(getFloat24(gstate.lpos[3 * light]), getFloat24(gstate.lpos[3 * light + 1]),getFloat24(gstate.lpos[3 * light + 2]));
			float diffuse_factor = Dot(L.Normalized(), vertex.worldnormal);

			if (gstate.getUVLS0() == (int)light)
				vertex.texturecoords.s() = (diffuse_factor + 1.f) / 2.f;

			if (gstate.getUVLS1() == (int)light)
				vertex.texturecoords.t() = (diffuse_factor + 1.f) / 2.f;
		}
	}

	if (!gstate.isLightingEnabled())
		return;

	for (unsigned int light = 0; light < 4; ++light) {
		if (!gstate.isLightChanEnabled(light))
			continue;

		// L =  vector from vertex to light source
		// TODO: Should transfer the light positions to world/view space for these calculations?
		Vec3<float> L = Vec3<float>(getFloat24(gstate.lpos[3 * light]), getFloat24(gstate.lpos[3 * light + 1]),getFloat24(gstate.lpos[3 * light + 2]));
		if (!gstate.isDirectionalLight(light)) {
			L -= vertex.worldpos;
		}
		float d = L.Normalize();

		float lka = getFloat24(gstate.latt[3 * light]);
		float lkb = getFloat24(gstate.latt[3 * light + 1]);
		float lkc = getFloat24(gstate.latt[3 * light + 2]);
		float att = 1.f;
		if (!gstate.isDirectionalLight(light)) {
			att = 1.f / (lka + lkb * d + lkc * d * d);
			if (att > 1.f) att = 1.f;
			if (att < 0.f) att = 0.f;
		}

		float spot = 1.f;
		if (gstate.isSpotLight(light)) {
			Vec3<float> dir = Vec3<float>(getFloat24(gstate.ldir[3 * light]), getFloat24(gstate.ldir[3 * light + 1]),getFloat24(gstate.ldir[3 * light + 2]));
			float _spot = Dot(dir.Normalized(), L);
			float cutoff = getFloat24(gstate.lcutoff[light]);
			if (_spot >= cutoff) {
				float conv = getFloat24(gstate.lconv[light]);
				spot = pow(_spot, conv);
			} else {
				spot = 0.f;
			}
		}

		// ambient lighting
		Vec3<float> lac = Vec3<float>::FromRGB(gstate.getLightAmbientColor(light));
		final_color += lac * mac * att * spot;

		// diffuse lighting
		Vec3<float> ldc = Vec3<float>::FromRGB(gstate.getDiffuseColor(light));
		Vec3<float> mdc = (materialupdate & 2) ? vcol0 : Vec3<float>::FromRGB(gstate.getMaterialDiffuse());

		float diffuse_factor = Dot(L, vertex.worldnormal);
		if (gstate.isUsingPoweredDiffuseLight(light)) {
			float k = gstate.getMaterialSpecularCoef();
			// TODO: Validate Tales of the World: Radiant Mythology (#2424.)
			// pow(0.0, 0.0) may be undefined, but the PSP seems to treat it as 1.0.
			if (diffuse_factor <= 0.0f && k == 0.0f) {
				diffuse_factor = 1.0f;
			} else {
				diffuse_factor = pow(diffuse_factor, k);
			}
		}

		if (diffuse_factor > 0.f) {
			final_color += ldc * mdc * diffuse_factor * att * spot;
		}

		if (gstate.isUsingSpecularLight(light)) {
			Vec3<float> H = L + Vec3<float>(0.f, 0.f, 1.f);

			Vec3<float> lsc = Vec3<float>::FromRGB(gstate.getSpecularColor(light));
			Vec3<float> msc = (materialupdate & 4) ? vcol0 : Vec3<float>::FromRGB(gstate.getMaterialSpecular());

			float specular_factor = Dot(H.Normalized(), vertex.worldnormal);
			float k = gstate.getMaterialSpecularCoef();
			specular_factor = pow(specular_factor, k);

			if (specular_factor > 0.f) {
				specular_color += lsc * msc * specular_factor * att * spot;
			}
		}
	}

	int maa = (materialupdate & 1) ? vertex.color0.a() : gstate.getMaterialAmbientA();
	int final_alpha = (gstate.getAmbientA() * maa) / 255;

	if (gstate.isUsingSecondaryColor()) {
		Vec3<int> final_color_int = (final_color.Clamp(0.0f, 1.0f) * 255.0f).Cast<int>();
		vertex.color0 = Vec4<int>(final_color_int, final_alpha);
		vertex.color1 = (specular_color.Clamp(0.0f, 1.0f) * 255.0f).Cast<int>();
	} else {
		Vec3<int> final_color_int = ((final_color + specular_color).Clamp(0.0f, 1.0f) * 255.0f).Cast<int>();
		vertex.color0 = Vec4<int>(final_color_int, final_alpha);
	}
}

#if PPSSPP_PLATFORM(WII)

void Prepare(PreparedState &state) {
  state.materialUpdate=gstate.materialupdate;
  state.emissive=Vec3<float>::FromRGB(gstate.getMaterialEmissive());
  state.ambient=Vec3<float>::FromRGB(gstate.getMaterialAmbientRGBA());
  state.diffuse=Vec3<float>::FromRGB(gstate.getMaterialDiffuse());
  state.specular=Vec3<float>::FromRGB(gstate.getMaterialSpecular());
  state.globalAmbient=Vec3<float>::FromRGB(gstate.getAmbientRGBA());
  state.specularCoef=gstate.getMaterialSpecularCoef();
  state.ambientAlpha=gstate.getAmbientA();state.materialAlpha=gstate.getMaterialAmbientA();
  state.environment=gstate.getUVGenMode()==GE_TEXMAP_ENVIRONMENT_MAP;
  state.uvLight0=gstate.getUVLS0();state.uvLight1=gstate.getUVLS1();
  state.enabled=gstate.isLightingEnabled();state.secondary=gstate.isUsingSecondaryColor();
  state.activeCount=0;
  for(unsigned i=0;i<4;++i) {
    auto &light=state.lights[i];
    light.position=Vec3<float>(getFloat24(gstate.lpos[3*i]),getFloat24(gstate.lpos[3*i+1]),getFloat24(gstate.lpos[3*i+2]));
    light.enabled=gstate.isLightChanEnabled(i);light.directional=gstate.isDirectionalLight(i);
    light.spot=gstate.isSpotLight(i);light.powered=gstate.isUsingPoweredDiffuseLight(i);
    light.useSpecular=gstate.isUsingSpecularLight(i);
    if(light.enabled)state.activeLights[state.activeCount++]=i;
    if((light.enabled&&light.directional)||state.environment)light.unitPosition=light.position.Normalized();
    if(light.enabled&&light.directional&&light.useSpecular)
      light.unitHalf=(light.unitPosition+Vec3<float>(0.f,0.f,1.f)).Normalized();
    if(light.enabled&&light.spot)light.unitDirection=Vec3<float>(getFloat24(gstate.ldir[3*i]),getFloat24(gstate.ldir[3*i+1]),getFloat24(gstate.ldir[3*i+2])).Normalized();
    light.constantAtt=getFloat24(gstate.latt[3*i]);light.linearAtt=getFloat24(gstate.latt[3*i+1]);light.quadraticAtt=getFloat24(gstate.latt[3*i+2]);
    light.cutoff=getFloat24(gstate.lcutoff[i]);light.exponent=getFloat24(gstate.lconv[i]);
    light.ambient=Vec3<float>::FromRGB(gstate.getLightAmbientColor(i));
    light.diffuse=Vec3<float>::FromRGB(gstate.getDiffuseColor(i));
    light.specular=Vec3<float>::FromRGB(gstate.getSpecularColor(i));
  }
}
void ProcessPrepared(VertexData& vertex, bool hasColor,const PreparedState &state)
{
	const int materialupdate = state.materialUpdate & (hasColor ? 7 : 0);

	Vec3<float> vcol0 = vertex.color0.rgb().Cast<float>() * Vec3<float>::AssignToAll(1.0f / 255.0f);
	Vec3<float> mec = state.emissive;

	Vec3<float> mac = (materialupdate & 1) ? vcol0 : state.ambient;
	Vec3<float> final_color = mec + mac * state.globalAmbient;
	Vec3<float> specular_color(0.0f, 0.0f, 0.0f);

  if(state.environment) {
    const float s=Dot(state.lights[state.uvLight0].unitPosition,vertex.worldnormal);
    const float t=Dot(state.lights[state.uvLight1].unitPosition,vertex.worldnormal);
    vertex.texturecoords.s()=(s+1.f)/2.f;
    vertex.texturecoords.t()=(t+1.f)/2.f;
  }

	if (!state.enabled)
		return;

	for (unsigned int active = 0; active < state.activeCount; ++active) {
    const unsigned light=state.activeLights[active];

		// L =  vector from vertex to light source
		// TODO: Should transfer the light positions to world/view space for these calculations?
		Vec3<float> L = state.lights[light].position;
		if (!state.lights[light].directional) {
			L -= vertex.worldpos;
		}
		float d = 0.f;
    if(state.lights[light].directional)L=state.lights[light].unitPosition;
    else d=L.Normalize();

		float lka = state.lights[light].constantAtt;
		float lkb = state.lights[light].linearAtt;
		float lkc = state.lights[light].quadraticAtt;
		float att = 1.f;
		if (!state.lights[light].directional) {
			att = 1.f / (lka + lkb * d + lkc * d * d);
			if (att > 1.f) att = 1.f;
			if (att < 0.f) att = 0.f;
		}

		float spot = 1.f;
		if (state.lights[light].spot) {
			Vec3<float> dir = state.lights[light].unitDirection;
			float _spot = Dot(dir, L);
			float cutoff = state.lights[light].cutoff;
			if (_spot >= cutoff) {
				float conv = state.lights[light].exponent;
				spot = pow(_spot, conv);
			} else {
				spot = 0.f;
			}
		}

		// ambient lighting
		Vec3<float> lac = state.lights[light].ambient;
		final_color += lac * mac * att * spot;

		// diffuse lighting
		Vec3<float> ldc = state.lights[light].diffuse;
		Vec3<float> mdc = (materialupdate & 2) ? vcol0 : state.diffuse;

		float diffuse_factor = Dot(L, vertex.worldnormal);
		if (state.lights[light].powered) {
			float k = state.specularCoef;
			// TODO: Validate Tales of the World: Radiant Mythology (#2424.)
			// pow(0.0, 0.0) may be undefined, but the PSP seems to treat it as 1.0.
			if (diffuse_factor <= 0.0f && k == 0.0f) {
				diffuse_factor = 1.0f;
			} else {
				diffuse_factor = pow(diffuse_factor, k);
			}
		}

		if (diffuse_factor > 0.f) {
			final_color += ldc * mdc * diffuse_factor * att * spot;
		}

		if (state.lights[light].useSpecular) {
			Vec3<float> H = L + Vec3<float>(0.f, 0.f, 1.f);

			Vec3<float> lsc = state.lights[light].specular;
			Vec3<float> msc = (materialupdate & 4) ? vcol0 : state.specular;

			float specular_factor = Dot(state.lights[light].directional?state.lights[light].unitHalf:H.Normalized(), vertex.worldnormal);
			float k = state.specularCoef;
			specular_factor = pow(specular_factor, k);

			if (specular_factor > 0.f) {
				specular_color += lsc * msc * specular_factor * att * spot;
			}
		}
	}

	int maa = (materialupdate & 1) ? vertex.color0.a() : state.materialAlpha;
	int final_alpha = (state.ambientAlpha * maa) / 255;

	if (state.secondary) {
		Vec3<int> final_color_int = (final_color.Clamp(0.0f, 1.0f) * 255.0f).Cast<int>();
		vertex.color0 = Vec4<int>(final_color_int, final_alpha);
		vertex.color1 = (specular_color.Clamp(0.0f, 1.0f) * 255.0f).Cast<int>();
	} else {
		Vec3<int> final_color_int = ((final_color + specular_color).Clamp(0.0f, 1.0f) * 255.0f).Cast<int>();
		vertex.color0 = Vec4<int>(final_color_int, final_alpha);
	}
}

#endif

} // namespace
