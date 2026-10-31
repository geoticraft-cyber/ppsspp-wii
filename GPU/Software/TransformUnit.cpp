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

#include "Common/MemoryUtil.h"
#include "Core/Host.h"
#include "Core/Config.h"
#include "GPU/GPUState.h"
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/Common/SplineCommon.h"

#include "GPU/Software/TransformUnit.h"
#include "GPU/Software/Clipper.h"
#include "GPU/Software/Lighting.h"
#if PPSSPP_PLATFORM(WII)
#include "Wii/Settings.h"
#include "Wii/RenderCompatibility.h"
#include "Core/System.h"
#include "Wii/GraphicsWork.h"
#include "Wii/VertexCacheStorage.h"
#include "Wii/CacheAdmission.h"
#include "Wii/VertexDecodeContext.h"
#include "Common/Log.h"
#include <cstring>
#include <new>
#endif

#define TRANSFORM_BUF_SIZE (65536 * 48)

#if PPSSPP_PLATFORM(WII)
// RECOVERED_1_2: conservative checked one-entry vertex decode cache.  The
// target explicitly labels this "On (checked)" and logs the verified source
// byte count.  Source bytes are compared before a decoded result is reused,
// so mutable PSP vertex buffers cannot return stale vertices.
struct WiiVertexDecodeCache {
	const void *base = nullptr;
	u32 vertexType = 0;
	u16 lower = 0, upper = 0;
	int sourceStride = 0, decodedStride = 0;
	Wii::VertexCacheStorage storage;
    // Includes all external decoder inputs, plus its cumulative side effects.
    u8 context[512]{};
    size_t contextBytes = 0;
    KnownVertexBounds bounds{};
    bool fullAlpha = false;
    Wii::CacheAdmission admission;
    uint64_t used=0;
	unsigned hits = 0;
	bool reported = false;
};
static WiiVertexDecodeCache wiiVertexCaches[4];
static unsigned wiiDecodeHits,wiiDecodeStores;
static uint64_t wiiCacheClock;
static unsigned wiiTransformHits;
#endif

unsigned SoftwareVertexCacheBytes() {
#if PPSSPP_PLATFORM(WII)
  unsigned bytes=0;for(const auto &cache:wiiVertexCaches)bytes+=cache.storage.ResidentBytes();return bytes;
#else
  return 0;
#endif
}
unsigned ReleaseSoftwareVertexCaches() {
#if PPSSPP_PLATFORM(WII)
  const unsigned bytes=SoftwareVertexCacheBytes();
  for(auto &cache:wiiVertexCaches) {cache.storage.Reset();cache.base=nullptr;cache.admission.Reset();}
  return bytes;
#else
  return 0;
#endif
}

unsigned SoftwareVertexTransformHits() {
#if PPSSPP_PLATFORM(WII)
    return wiiTransformHits;
#else
    return 0;
#endif
}

unsigned SoftwareVertexCacheStores() {
#if PPSSPP_PLATFORM(WII)
    return wiiDecodeStores;
#else
    return 0;
#endif
}

unsigned SoftwareVertexCacheHits() {
#if PPSSPP_PLATFORM(WII)
	return wiiDecodeHits;
#else
	return 0;
#endif
}

TransformUnit::TransformUnit() {
#if PPSSPP_PLATFORM(WII)
	buf = nullptr;
#else
	buf = (u8 *)AllocateMemoryPages(TRANSFORM_BUF_SIZE, MEM_PROT_READ | MEM_PROT_WRITE);
	bufBytes=TRANSFORM_BUF_SIZE;
#endif
}

TransformUnit::~TransformUnit() {
	FreeMemoryPages(buf, bufBytes);
}

SoftwareDrawEngine::SoftwareDrawEngine() {
#if !PPSSPP_PLATFORM(WII)
	decoded = (u8 *)AllocateMemoryPages(DECODED_VERTEX_BUFFER_SIZE, MEM_PROT_READ | MEM_PROT_WRITE);
	decIndex = (u16 *)AllocateMemoryPages(DECODED_INDEX_BUFFER_SIZE, MEM_PROT_READ | MEM_PROT_WRITE);
	splineBuffer = (u8 *)AllocateMemoryPages(SPLINE_BUFFER_SIZE, MEM_PROT_READ | MEM_PROT_WRITE);
#endif
}

SoftwareDrawEngine::~SoftwareDrawEngine() {
#if PPSSPP_PLATFORM(WII)
    wiiTransformHits=wiiDecodeHits=wiiDecodeStores=0;wiiCacheClock=0;
    for(auto &cache:wiiVertexCaches) {
        cache.storage.Reset();cache.base=nullptr;cache.contextBytes=0;
        cache.admission.Reset();cache.used=0;cache.reported=false;
    }
#endif
	FreeMemoryPages(decoded, DECODED_VERTEX_BUFFER_SIZE);
	FreeMemoryPages(decIndex, DECODED_INDEX_BUFFER_SIZE);
	FreeMemoryPages(splineBuffer, SPLINE_BUFFER_SIZE);
}

bool SoftwareDrawEngine::EnsureScratchBuffers(bool patches) {
#if PPSSPP_PLATFORM(WII)
	if (!decoded) decoded=(u8 *)AllocateMemoryPages(DECODED_VERTEX_BUFFER_SIZE, MEM_PROT_READ | MEM_PROT_WRITE);
	if (patches && !splineBuffer) splineBuffer=(u8 *)AllocateMemoryPages(SPLINE_BUFFER_SIZE, MEM_PROT_READ | MEM_PROT_WRITE);
	if (patches && !quadIndices_) quadIndices_=new(std::nothrow) u16[6 * 65536];
	if (!decoded || (patches && (!splineBuffer || !quadIndices_))) {
		ERROR_LOG(G3D,"Cannot allocate software patch/bounding-box scratch");
		return false;
	}
#endif
	return true;
}

void SoftwareDrawEngine::DispatchFlush() {
}

void SoftwareDrawEngine::DispatchSubmitPrim(void *verts, void *inds, GEPrimitiveType prim, int vertexCount, u32 vertTypeID, int *bytesRead) {
	transformUnit.SubmitPrimitive(verts, inds, prim, vertexCount, vertTypeID, bytesRead, this);
}

VertexDecoder *SoftwareDrawEngine::FindVertexDecoder(u32 vtype) {
	const u32 vertTypeID = (vtype & 0xFFFFFF) | (gstate.getUVGenMode() << 24);
	return DrawEngineCommon::GetVertexDecoder(vertTypeID);
}

WorldCoords TransformUnit::ModelToWorld(const ModelCoords& coords)
{
	Mat3x3<float> world_matrix(gstate.worldMatrix);
	return WorldCoords(world_matrix * coords) + Vec3<float>(gstate.worldMatrix[9], gstate.worldMatrix[10], gstate.worldMatrix[11]);
}

WorldCoords TransformUnit::ModelToWorldNormal(const ModelCoords& coords)
{
	Mat3x3<float> world_matrix(gstate.worldMatrix);
	return WorldCoords(world_matrix * coords);
}

ViewCoords TransformUnit::WorldToView(const WorldCoords& coords)
{
	Mat3x3<float> view_matrix(gstate.viewMatrix);
	return ViewCoords(view_matrix * coords) + Vec3<float>(gstate.viewMatrix[9], gstate.viewMatrix[10], gstate.viewMatrix[11]);
}

ClipCoords TransformUnit::ViewToClip(const ViewCoords& coords)
{
	Vec4<float> coords4(coords.x, coords.y, coords.z, 1.0f);
	Mat4x4<float> projection_matrix(gstate.projMatrix);
	return ClipCoords(projection_matrix * coords4);
}

static inline ScreenCoords ClipToScreenInternal(const ClipCoords& coords, bool *outside_range_flag) {
	ScreenCoords ret;

	// Parameters here can seem invalid, but the PSP is fine with negative viewport widths etc.
	// The checking that OpenGL and D3D do is actually quite superflous as the calculations still "work"
	// with some pretty crazy inputs, which PSP games are happy to do at times.
	float xScale = gstate.getViewportXScale();
	float xCenter = gstate.getViewportXCenter();
	float yScale = gstate.getViewportYScale();
	float yCenter = gstate.getViewportYCenter();
	float zScale = gstate.getViewportZScale();
	float zCenter = gstate.getViewportZCenter();

	float x = coords.x * xScale / coords.w + xCenter;
	float y = coords.y * yScale / coords.w + yCenter;
	float z = coords.z * zScale / coords.w + zCenter;

	// This matches hardware tests - depth is clamped when this flag is on.
	if (gstate.isClippingEnabled()) {
		if (z < 0.f)
			z = 0.f;
		if (z > 65535.f)
			z = 65535.f;
	}

	if (outside_range_flag && (x > 4095.9375f || y > 4095.9375f || x < 0 || y < 0 || z < 0 || z > 65535.f))
		*outside_range_flag = true;

	// 16 = 0xFFFF / 4095.9375
	// Round up at 0.625 to the nearest subpixel.
	return ScreenCoords(x * 16.0f + 0.375f, y * 16.0f + 0.375f, z);
}

ScreenCoords TransformUnit::ClipToScreen(const ClipCoords& coords)
{
	return ClipToScreenInternal(coords, nullptr);
}

DrawingCoords TransformUnit::ScreenToDrawing(const ScreenCoords& coords)
{
	DrawingCoords ret;
	// TODO: What to do when offset > coord?
	ret.x = ((s32)coords.x - gstate.getOffsetX16()) / 16;
	ret.y = ((s32)coords.y - gstate.getOffsetY16()) / 16;
	ret.z = coords.z;
	return ret;
}

ScreenCoords TransformUnit::DrawingToScreen(const DrawingCoords& coords)
{
	ScreenCoords ret;
	ret.x = (u32)coords.x * 16 + gstate.getOffsetX16();
	ret.y = (u32)coords.y * 16 + gstate.getOffsetY16();
	ret.z = coords.z;
	return ret;
}

VertexData TransformUnit::ReadVertex(VertexReader& vreader)
{
	VertexData vertex{};
  const bool through=gstate.isModeThrough();
  const bool lightNormals=!through&&(gstate.isLightingEnabled()||gstate.getUVGenMode()==GE_TEXMAP_ENVIRONMENT_MAP);
  const bool projectedNormals=!through&&gstate.getUVGenMode()==GE_TEXMAP_TEXTURE_MATRIX&&
    (gstate.getUVProjMode()==GE_PROJMAP_NORMAL||gstate.getUVProjMode()==GE_PROJMAP_NORMALIZED_NORMAL);
  const bool readNormal=vreader.hasNormal()&&(lightNormals||projectedNormals);

	float pos[3];
	// VertexDecoder normally scales z, but we want it unscaled.
	vreader.ReadPosThroughZ16(pos);

	if (!gstate.isModeClear() && gstate.isTextureMapEnabled() && vreader.hasUV()) {
		float uv[2];
		vreader.ReadUV(uv);
		vertex.texturecoords = Vec2<float>(uv[0], uv[1]);
	}

	if (readNormal) {
		float normal[3];
		vreader.ReadNrm(normal);
		vertex.normal = Vec3<float>(normal[0], normal[1], normal[2]);

		if (gstate.areNormalsReversed())
			vertex.normal = -vertex.normal;
	}

	if (vertTypeIsSkinningEnabled(gstate.vertType) && !gstate.isModeThrough()) {
		float W[8] = { 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f };
		vreader.ReadWeights(W);

		Vec3<float> tmppos(0.f, 0.f, 0.f);
		Vec3<float> tmpnrm(0.f, 0.f, 0.f);

		for (int i = 0; i < vertTypeGetNumBoneWeights(gstate.vertType); ++i) {
			Mat3x3<float> bone(&gstate.boneMatrix[12*i]);
			tmppos += (bone * ModelCoords(pos[0], pos[1], pos[2]) + Vec3<float>(gstate.boneMatrix[12*i+9], gstate.boneMatrix[12*i+10], gstate.boneMatrix[12*i+11])) * W[i];
			if (readNormal)
				tmpnrm += (bone * vertex.normal) * W[i];
		}

		pos[0] = tmppos.x;
		pos[1] = tmppos.y;
		pos[2] = tmppos.z;
		if (readNormal)
			vertex.normal = tmpnrm;
	}

	if (vreader.hasColor0()) {
		float col[4];
		vreader.ReadColor0(col);
		vertex.color0 = Vec4<int>(col[0]*255, col[1]*255, col[2]*255, col[3]*255);
	} else {
		vertex.color0 = Vec4<int>(gstate.getMaterialAmbientR(), gstate.getMaterialAmbientG(), gstate.getMaterialAmbientB(), gstate.getMaterialAmbientA());
	}

	if (vreader.hasColor1()) {
		float col[3];
		vreader.ReadColor1(col);
		vertex.color1 = Vec3<int>(col[0]*255, col[1]*255, col[2]*255);
	} else {
		vertex.color1 = Vec3<int>(0, 0, 0);
	}

	if (!gstate.isModeThrough()) {
		vertex.modelpos = ModelCoords(pos[0], pos[1], pos[2]);
		vertex.worldpos = WorldCoords(TransformUnit::ModelToWorld(vertex.modelpos));
		ModelCoords viewpos = TransformUnit::WorldToView(vertex.worldpos);
		vertex.clippos = ClipCoords(TransformUnit::ViewToClip(viewpos));
		if (gstate.isFogEnabled()) {
			// TODO: Validate inf/nan.
			vertex.fogdepth = (viewpos.z + getFloat24(gstate.fog1)) * getFloat24(gstate.fog2);
		} else {
			vertex.fogdepth = 1.0f;
		}
		vertex.screenpos = ClipToScreenInternal(vertex.clippos, &outside_range_flag);

		if (lightNormals && readNormal) {
			vertex.worldnormal = TransformUnit::ModelToWorldNormal(vertex.normal);
			// TODO: Isn't there a flag that controls whether to normalize the normal?
			vertex.worldnormal /= vertex.worldnormal.Length();
		} else {
			vertex.worldnormal = Vec3<float>(0.0f, 0.0f, 1.0f);
		}

		if(lightNormals) {
#if PPSSPP_PLATFORM(WII)
      if(preparedLighting)Lighting::ProcessPrepared(vertex,vreader.hasColor0(),*preparedLighting);
      else
#endif
        Lighting::Process(vertex,vreader.hasColor0());
    }
	} else {
		vertex.screenpos.x = (int)(pos[0] * 16) + gstate.getOffsetX16();
		vertex.screenpos.y = (int)(pos[1] * 16) + gstate.getOffsetY16();
		vertex.screenpos.z = pos[2];
		vertex.clippos.w = 1.f;
		vertex.fogdepth = 1.f;
	}

	return vertex;
}

#define START_OPEN_U 1
#define END_OPEN_U 2
#define START_OPEN_V 4
#define END_OPEN_V 8

struct SplinePatch {
	VertexData points[16];
	int type;
	int pad[3];
};

void TransformUnit::SubmitPrimitive(void* vertices, void* indices, GEPrimitiveType prim_type, int vertex_count, u32 vertex_type, int *bytesRead, SoftwareDrawEngine *drawEngine)
{
  if(vertex_count<=0) {if(bytesRead)*bytesRead=0;return;}
	VertexDecoder &vdecoder = *drawEngine->FindVertexDecoder(vertex_type);
	const DecVtxFormat &vtxfmt = vdecoder.GetDecVtxFmt();

	if (bytesRead)
		*bytesRead = vertex_count * vdecoder.VertexSize();

	// Frame skipping.  RECOVERED_1_2 adds the optional conservative off-screen
	// buffer-effects skip.  It is intentionally opt-in because some games use
	// non-displayed framebuffers as later texture inputs.
#if PPSSPP_PLATFORM(WII)
	if (Wii::SkipSoftwareDraw(PSP_CoreParameter().compat.flags(),
		gstate_c.skipDrawReason, Wii::SkipBufferEffects())) {
		return;
	}
#else
	if (gstate_c.skipDrawReason & SKIPDRAW_SKIPFRAME) {
		return;
	}
#endif

	u16 index_lower_bound = 0;
	u16 index_upper_bound = vertex_count - 1;
	IndexConverter idxConv(vertex_type, indices);

	if (indices)
		GetIndexBounds(indices, vertex_count, vertex_type, &index_lower_bound, &index_upper_bound);
#if PPSSPP_PLATFORM(WII)
	const unsigned needed=unsigned(index_upper_bound-index_lower_bound+1)*unsigned(vtxfmt.stride);
	if (needed>bufBytes || (Wii::GetSettings().lowMemory && bufBytes>std::max(needed,65536u)*2)) {
		const unsigned capacity=(needed+65535)&~65535u;
		FreeMemoryPages(buf,bufBytes);buf=nullptr;bufBytes=0;
		buf=(u8 *)AllocateMemoryPages(capacity,MEM_PROT_READ|MEM_PROT_WRITE);
		if(!buf) {
      // This is before any borrowed vertex stream is in use. Disposable cache
      // entries can be reclaimed without dropping guest RAM or geometry.
      if(ReleaseSoftwareVertexCaches()) {
        ++Wii::GetGraphicsWork().decodeRetries;
        buf=(u8 *)AllocateMemoryPages(capacity,MEM_PROT_READ|MEM_PROT_WRITE);
      }
      if(!buf){ERROR_LOG(G3D,"Cannot allocate %u vertex decode bytes",capacity);return;}
    }
		bufBytes=capacity;
    Wii::GetGraphicsWork().decodeScratchPeak=std::max(Wii::GetGraphicsWork().decodeScratchPeak,bufBytes);
	}
	bool decodedReady = false;
    const u8 *drawBuffer=buf;
    const bool low=Wii::GetSettings().lowMemory;
    const unsigned active=low?1:4;
    const size_t sourceLimit=(low?32u:64u)*1024,decodedLimit=(low?64u:128u)*1024;
    const int sourceStride=vdecoder.VertexSize(),decodedStride=vtxfmt.stride;
    const size_t count=size_t(index_upper_bound-index_lower_bound+1);
    const size_t sourceBytes=count*size_t(sourceStride),decodedBytes=count*size_t(decodedStride);
    for(unsigned i=0;i<4;++i) {
        wiiVertexCaches[i].storage.Configure(sourceLimit+decodedLimit);
        if(i>=active){wiiVertexCaches[i].storage.Reset();wiiVertexCaches[i].base=nullptr;}
    }
    if(Wii::VertexCacheHack()&&sourceBytes&&sourceBytes<=sourceLimit&&decodedBytes<=decodedLimit) {
        const u8 *sourceBegin=static_cast<const u8 *>(vertices)+size_t(index_lower_bound)*sourceStride;
        u8 context[512];const size_t contextBytes=Wii::CaptureVertexDecodeContext(context);
        WiiVertexDecodeCache *selected=nullptr,*victim=&wiiVertexCaches[0];
        for(unsigned i=0;i<active;++i) {
            auto &candidate=wiiVertexCaches[i];
            if(candidate.used<victim->used)victim=&candidate;
            if(candidate.base==vertices&&candidate.vertexType==vertex_type&&candidate.lower==index_lower_bound&&
               candidate.upper==index_upper_bound&&candidate.sourceStride==sourceStride&&
               candidate.decodedStride==decodedStride&&candidate.contextBytes==contextBytes&&
               !memcmp(candidate.context,context,contextBytes)){selected=&candidate;break;}
        }
        const bool matched=selected!=nullptr;
        if(!selected) {
            selected=victim;selected->storage.Reset();selected->admission.Reset();
            selected->base=vertices;selected->vertexType=vertex_type;
            selected->lower=index_lower_bound;selected->upper=index_upper_bound;
            selected->sourceStride=sourceStride;selected->decodedStride=decodedStride;
            memcpy(selected->context,context,contextBytes);selected->contextBytes=contextBytes;
        }
        auto &cache=*selected;cache.used=++wiiCacheClock;
        const bool previouslyFailed=cache.storage.Failed();
        const auto *cached=matched?cache.storage.Borrow(sourceBegin,sourceBytes,decodedBytes):nullptr;
        if(cached) {
            drawBuffer=cached;Wii::GetGraphicsWork().vertexCopyAvoided+=decodedBytes;
            Wii::MergeVertexDecodeEffects(cache.bounds,cache.fullAlpha);
            ++wiiDecodeHits;decodedReady=true;
        } else {
            // Capture this decode's output effects independently of prior draws.
            const KnownVertexBounds beforeBounds=gstate_c.vertBounds;
            const bool beforeAlpha=gstate_c.vertexFullAlpha;
            gstate_c.vertBounds={65535,65535,0,0};gstate_c.vertexFullAlpha=true;
            vdecoder.DecodeVerts(buf,vertices,index_lower_bound,index_upper_bound);
            Wii::GetGraphicsWork().verticesDecoded+=count;
            cache.bounds=gstate_c.vertBounds;cache.fullAlpha=gstate_c.vertexFullAlpha;
            gstate_c.vertBounds=beforeBounds;gstate_c.vertexFullAlpha=beforeAlpha;
            Wii::MergeVertexDecodeEffects(cache.bounds,cache.fullAlpha);
            // Never write one-shot or continuously changing data to USB. A hash
            // collision can only admit a fresh snapshot; hits use exact bytes.
            cache.storage.Invalidate();
            if(cache.admission.Observe(sourceBegin,sourceBytes)&&
               cache.storage.Store(sourceBegin,sourceBytes,buf,decodedBytes)) {
              ++wiiDecodeStores;
              auto &work=Wii::GetGraphicsWork();work.vertexCachePeak=std::max(work.vertexCachePeak,SoftwareVertexCacheBytes());
            }
            decodedReady=true;
            if(!previouslyFailed&&cache.storage.Failed())
                WARN_LOG(G3D,"USB vertex cache I/O failed; uncached decoding, no SD fallback");
        }
    } else if(!Wii::VertexCacheHack()) {
        for(auto &cache:wiiVertexCaches){cache.storage.Reset();cache.base=nullptr;cache.admission.Reset();}
    }
    if(!decodedReady) {
      vdecoder.DecodeVerts(buf,vertices,index_lower_bound,index_upper_bound);
      Wii::GetGraphicsWork().verticesDecoded+=count;
    }
#else
	vdecoder.DecodeVerts(buf, vertices, index_lower_bound, index_upper_bound);
#endif

#if PPSSPP_PLATFORM(WII)
  VertexReader vreader(drawBuffer,vtxfmt,vertex_type);
#else
  VertexReader vreader(buf,vtxfmt,vertex_type);
#endif

#if PPSSPP_PLATFORM(WII)
    Lighting::PreparedState lightState;
    const bool needsLighting=!gstate.isModeThrough()&&(gstate.isLightingEnabled()||gstate.getUVGenMode()==GE_TEXMAP_ENVIRONMENT_MAP);
    if(needsLighting) {Lighting::Prepare(lightState);++Wii::GetGraphicsWork().preparedLightingDraws;}
    // Also protects early returns and keeps debugger/spline reads on the
    // reference path outside this primitive submission.
    struct LightingScope {
      const Lighting::PreparedState *&slot;
      const Lighting::PreparedState *previous;
      ~LightingScope() {slot=previous;}
    } lightScope{preparedLighting,preparedLighting};
    preparedLighting=needsLighting?&lightState:nullptr;
    vertexMemo.Reset(Wii::GetSettings().lowMemory);
    unsigned drawRequests=0,drawTransforms=0;
#endif
    auto readVertex = [&](int vtx) -> VertexData {
        const u32 index=indices?idxConv.convert(vtx)-index_lower_bound:u32(vtx);
#if PPSSPP_PLATFORM(WII)
        ++drawRequests;
        VertexData vertex{};
        if(indices && vertexMemo.Load(index,vertex,outside_range_flag)) {++wiiTransformHits;return vertex;}
        const bool pending=outside_range_flag;
        outside_range_flag=false;
        vreader.Goto(index);
        vertex=ReadVertex(vreader);++drawTransforms;
        if(indices)vertexMemo.Store(index,vertex,outside_range_flag);
        outside_range_flag=outside_range_flag||pending;
        return vertex;
#else
        vreader.Goto(index);
        return ReadVertex(vreader);
#endif
    };

	const int max_vtcs_per_prim = 3;
#if PPSSPP_PLATFORM(WII)
  VertexData *data=primitiveVertices;
  int &data_index=primitiveIndex;
  GEPrimitiveType &prev_prim=previousPrimitive;
#else
  static VertexData data[max_vtcs_per_prim];
  static int data_index=0;
  static GEPrimitiveType prev_prim=GE_PRIM_POINTS;
#endif
	if (prim_type != GE_PRIM_KEEP_PREVIOUS) {
#if PPSSPP_PLATFORM(WII)
    outside_range_flag=false;
#endif
		data_index = 0;
		prev_prim = prim_type;
	} else {
		prim_type = prev_prim;
	}

	int vtcs_per_prim;
	switch (prim_type) {
	case GE_PRIM_POINTS: vtcs_per_prim = 1; break;
	case GE_PRIM_LINES: vtcs_per_prim = 2; break;
	case GE_PRIM_TRIANGLES: vtcs_per_prim = 3; break;
	case GE_PRIM_RECTANGLES: vtcs_per_prim = 2; break;
	default: vtcs_per_prim = 0; break;
	}

	// TODO: Do this in two passes - first process the vertices (before indexing/stripping),
	// then resolve the indices. This lets us avoid transforming shared vertices twice.

	switch (prim_type) {
	case GE_PRIM_POINTS:
	case GE_PRIM_LINES:
	case GE_PRIM_TRIANGLES:
	case GE_PRIM_RECTANGLES:
		{
			for (int vtx = 0; vtx < vertex_count; ++vtx) {
				data[data_index++] = readVertex(vtx);
				if (data_index < vtcs_per_prim) {
					// Keep reading.  Note: an incomplete prim will stay read for GE_PRIM_KEEP_PREVIOUS.
					continue;
				}

				// Okay, we've got enough verts.  Reset the index for next time.
				data_index = 0;
				if (outside_range_flag) {
					// Cull the prim if it was outside, and move to the next prim.
					outside_range_flag = false;
					continue;
				}

				switch (prim_type) {
				case GE_PRIM_TRIANGLES:
				{
					if (!gstate.isCullEnabled() || gstate.isModeClear()) {
						Clipper::ProcessTriangle(data[0], data[1], data[2]);
						Clipper::ProcessTriangle(data[2], data[1], data[0]);
					} else if (!gstate.getCullMode()) {
						Clipper::ProcessTriangle(data[2], data[1], data[0]);
					} else {
						Clipper::ProcessTriangle(data[0], data[1], data[2]);
					}
					break;
				}

				case GE_PRIM_RECTANGLES:
					Clipper::ProcessRect(data[0], data[1]);
					break;

				case GE_PRIM_LINES:
					Clipper::ProcessLine(data[0], data[1]);
					break;

				case GE_PRIM_POINTS:
					Clipper::ProcessPoint(data[0]);
					break;

				default:
					_dbg_assert_msg_(G3D, false, "Unexpected prim type: %d", prim_type);
				}
			}
			break;
		}

	case GE_PRIM_LINE_STRIP:
		{
			// Don't draw a line when loading the first vertex.
			// If data_index is 1 or 2, etc., it means we're continuing a line strip.
			int skip_count = data_index == 0 ? 1 : 0;
			for (int vtx = 0; vtx < vertex_count; ++vtx) {
				data[(data_index++) & 1] = readVertex(vtx);
				if (outside_range_flag) {
					// Drop all primitives containing the current vertex
					skip_count = 2;
					outside_range_flag = false;
					continue;
				}

				if (skip_count) {
					--skip_count;
				} else {
					// We already incremented data_index, so data_index & 1 is previous one.
					Clipper::ProcessLine(data[data_index & 1], data[(data_index & 1) ^ 1]);
				}
			}
			break;
		}

	case GE_PRIM_TRIANGLE_STRIP:
		{
			// Don't draw a triangle when loading the first two vertices.
			int skip_count = data_index >= 2 ? 0 : 2 - data_index;

			for (int vtx = 0; vtx < vertex_count; ++vtx) {
				data[(data_index++) % 3] = readVertex(vtx);
				if (outside_range_flag) {
					// Drop all primitives containing the current vertex
					skip_count = 2;
					outside_range_flag = false;
					continue;
				}

				if (skip_count) {
					--skip_count;
					continue;
				}

				if (!gstate.isCullEnabled() || gstate.isModeClear()) {
					Clipper::ProcessTriangle(data[0], data[1], data[2]);
					Clipper::ProcessTriangle(data[2], data[1], data[0]);
				} else if ((!gstate.getCullMode()) ^ ((data_index - 1) % 2)) {
					// We need to reverse the vertex order for each second primitive,
					// but we additionally need to do that for every primitive if CCW cullmode is used.
					Clipper::ProcessTriangle(data[2], data[1], data[0]);
				} else {
					Clipper::ProcessTriangle(data[0], data[1], data[2]);
				}
			}
			break;
		}

	case GE_PRIM_TRIANGLE_FAN:
		{
			// Don't draw a triangle when loading the first two vertices.
			// (this doesn't count the central one.)
			int skip_count = data_index <= 1 ? 1 : 0;
			int start_vtx = 0;

			// Only read the central vertex if we're not continuing.
			if (data_index == 0) {
				data[0] = readVertex(0);
				data_index++;
				start_vtx = 1;
			}

			for (int vtx = start_vtx; vtx < vertex_count; ++vtx) {
				data[2 - ((data_index++) % 2)] = readVertex(vtx);
				if (outside_range_flag) {
					// Drop all primitives containing the current vertex
					skip_count = 2;
					outside_range_flag = false;
					continue;
				}

				if (skip_count) {
					--skip_count;
					continue;
				}

				if (!gstate.isCullEnabled() || gstate.isModeClear()) {
					Clipper::ProcessTriangle(data[0], data[1], data[2]);
					Clipper::ProcessTriangle(data[2], data[1], data[0]);
				} else if ((!gstate.getCullMode()) ^ ((data_index - 1) % 2)) {
					// We need to reverse the vertex order for each second primitive,
					// but we additionally need to do that for every primitive if CCW cullmode is used.
					Clipper::ProcessTriangle(data[2], data[1], data[0]);
				} else {
					Clipper::ProcessTriangle(data[0], data[1], data[2]);
				}
			}
			break;
		}

	default:
		ERROR_LOG(G3D, "Unexpected prim type: %d", prim_type);
		break;
	}

#if PPSSPP_PLATFORM(WII)
  // Keep strip/fan parity and ring position without an unbounded signed
  // counter across KEEP_PREVIOUS submissions.
  if(primitiveIndex>=12)primitiveIndex=6+primitiveIndex%6;
  auto &work=Wii::GetGraphicsWork();
  work.vertexRequests+=drawRequests;work.verticesTransformed+=drawTransforms;
#endif
	host->GPUNotifyDraw();
}

// TODO: This probably is not the best interface.
// Also, we should try to merge this into the similar function in DrawEngineCommon.
bool TransformUnit::GetCurrentSimpleVertices(int count, std::vector<GPUDebugVertex> &vertices, std::vector<u16> &indices) {
	// This is always for the current vertices.
	u16 indexLowerBound = 0;
	u16 indexUpperBound = count - 1;

	if ((gstate.vertType & GE_VTYPE_IDX_MASK) != GE_VTYPE_IDX_NONE) {
		const u8 *inds = Memory::GetPointer(gstate_c.indexAddr);
		const u16_le *inds16 = (const u16_le *)inds;
		const u32_le *inds32 = (const u32_le *)inds;

		if (inds) {
			GetIndexBounds(inds, count, gstate.vertType, &indexLowerBound, &indexUpperBound);
			indices.resize(count);
			switch (gstate.vertType & GE_VTYPE_IDX_MASK) {
			case GE_VTYPE_IDX_8BIT:
				for (int i = 0; i < count; ++i) {
					indices[i] = inds[i];
				}
				break;
			case GE_VTYPE_IDX_16BIT:
				for (int i = 0; i < count; ++i) {
					indices[i] = inds16[i];
				}
				break;
			case GE_VTYPE_IDX_32BIT:
				WARN_LOG_REPORT_ONCE(simpleIndexes32, G3D, "SimpleVertices: Decoding 32-bit indexes");
				for (int i = 0; i < count; ++i) {
					// These aren't documented and should be rare.  Let's bounds check each one.
					if (inds32[i] != (u16)inds32[i]) {
						ERROR_LOG_REPORT_ONCE(simpleIndexes32Bounds, G3D, "SimpleVertices: Index outside 16-bit range");
					}
					indices[i] = (u16)inds32[i];
				}
				break;
			}
		} else {
			indices.clear();
		}
	} else {
		indices.clear();
	}

	static std::vector<u32> temp_buffer;
	static std::vector<SimpleVertex> simpleVertices;
	temp_buffer.resize(65536 * 24 / sizeof(u32));
	simpleVertices.resize(indexUpperBound + 1);

	VertexDecoder vdecoder;
	VertexDecoderOptions options{};
	vdecoder.SetVertexType(gstate.vertType, options);
	DrawEngineCommon::NormalizeVertices((u8 *)(&simpleVertices[0]), (u8 *)(&temp_buffer[0]), Memory::GetPointer(gstate_c.vertexAddr), &vdecoder, indexLowerBound, indexUpperBound, gstate.vertType);

	float world[16];
	float view[16];
	float worldview[16];
	float worldviewproj[16];
	ConvertMatrix4x3To4x4(world, gstate.worldMatrix);
	ConvertMatrix4x3To4x4(view, gstate.viewMatrix);
	Matrix4ByMatrix4(worldview, world, view);
	Matrix4ByMatrix4(worldviewproj, worldview, gstate.projMatrix);

	vertices.resize(indexUpperBound + 1);
	for (int i = indexLowerBound; i <= indexUpperBound; ++i) {
		const SimpleVertex &vert = simpleVertices[i];

		if (gstate.isModeThrough()) {
			if (gstate.vertType & GE_VTYPE_TC_MASK) {
				vertices[i].u = vert.uv[0];
				vertices[i].v = vert.uv[1];
			} else {
				vertices[i].u = 0.0f;
				vertices[i].v = 0.0f;
			}
			vertices[i].x = vert.pos.x;
			vertices[i].y = vert.pos.y;
			vertices[i].z = vert.pos.z;
			if (gstate.vertType & GE_VTYPE_COL_MASK) {
				memcpy(vertices[i].c, vert.color, sizeof(vertices[i].c));
			} else {
				memset(vertices[i].c, 0, sizeof(vertices[i].c));
			}
		} else {
			float clipPos[4];
			Vec3ByMatrix44(clipPos, vert.pos.AsArray(), worldviewproj);
			ScreenCoords screenPos = ClipToScreen(clipPos);
			DrawingCoords drawPos = ScreenToDrawing(screenPos);

			if (gstate.vertType & GE_VTYPE_TC_MASK) {
				vertices[i].u = vert.uv[0] * (float)gstate.getTextureWidth(0);
				vertices[i].v = vert.uv[1] * (float)gstate.getTextureHeight(0);
			} else {
				vertices[i].u = 0.0f;
				vertices[i].v = 0.0f;
			}
			vertices[i].x = drawPos.x;
			vertices[i].y = drawPos.y;
			vertices[i].z = drawPos.z;
			if (gstate.vertType & GE_VTYPE_COL_MASK) {
				memcpy(vertices[i].c, vert.color, sizeof(vertices[i].c));
			} else {
				memset(vertices[i].c, 0, sizeof(vertices[i].c));
			}
		}
	}

	// The GE debugger expects these to be set.
	gstate_c.curTextureWidth = gstate.getTextureWidth(0);
	gstate_c.curTextureHeight = gstate.getTextureHeight(0);

	return true;
}
