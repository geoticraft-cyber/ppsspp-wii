// SPDX-License-Identifier: GPL-2.0-or-later
// Original clear implementation retained as a regression oracle.
#pragma once
static inline void PreviousSetPixelDepth(int x,int y,u16 v) {depthbuf.Set16(x,y,gstate.DepthBufStride(),v);}
__attribute__((noinline)) static void PreviousClearRectangle(const VertexData &v0, const VertexData &v1)
{
	int minX = std::min(v0.screenpos.x, v1.screenpos.x) & ~0xF;
	int minY = std::min(v0.screenpos.y, v1.screenpos.y) & ~0xF;
	int maxX = (std::max(v0.screenpos.x, v1.screenpos.x) + 0xF) & ~0xF;
	int maxY = (std::max(v0.screenpos.y, v1.screenpos.y) + 0xF) & ~0xF;

	DrawingCoords scissorTL(gstate.getScissorX1(), gstate.getScissorY1(), 0);
	DrawingCoords scissorBR(gstate.getScissorX2()+1, gstate.getScissorY2()+1, 0);
	minX = std::max(minX, (int)TransformUnit::DrawingToScreen(scissorTL).x);
	maxX = std::max(0, std::min(maxX, (int)TransformUnit::DrawingToScreen(scissorBR).x));
	minY = std::max(minY, (int)TransformUnit::DrawingToScreen(scissorTL).y);
	maxY = std::max(0, std::min(maxY, (int)TransformUnit::DrawingToScreen(scissorBR).y));

	const int w = (maxX - minX) / 16;
	if (w <= 0)
		return;

	if (gstate.isClearModeDepthMask()) {
		ScreenCoords pprime(minX, minY, 0);
		const u16 z = v1.screenpos.z;
		const int stride = gstate.DepthBufStride();

		for (pprime.y = minY; pprime.y < maxY; pprime.y += 16) {
			DrawingCoords p = TransformUnit::ScreenToDrawing(pprime);

			if ((z & 0xFF) == (z >> 8)) {
				#if PPSSPP_PLATFORM(WII)
        Wii::FillBuffer(depthbuf.data,p.x,p.y,stride,2,w,uint8_t(z));
#else
        void *row = &depthbuf.as16[p.x + p.y * stride];
        memset(row,z,w*2);
#endif
			} else {
				for (int x = 0; x < w; ++x) {
					PreviousSetPixelDepth(p.x + x, p.y, z);
				}
			}
		}
	}

	const u32 new_color = v1.color0.ToRGBA();
	u16 new_color16;

	// Note: this stays 0xFFFFFFFF if keeping color and alpha, even for 16-bit.
	u32 keepOldMask = 0xFFFFFFFF;
	switch (gstate.FrameBufFormat()) {
	case GE_FORMAT_565:
		new_color16 = RGBA8888ToRGB565(new_color);
		if (gstate.isClearModeColorMask())
			keepOldMask = 0;
		break;

	case GE_FORMAT_5551:
		new_color16 = RGBA8888ToRGBA5551(new_color);
		if (gstate.isClearModeColorMask())
			keepOldMask &= 0x00008000;
		if (gstate.isClearModeAlphaMask())
			keepOldMask &= 0x00007FFF;
		break;

	case GE_FORMAT_4444:
		new_color16 = RGBA8888ToRGBA4444(new_color);
		if (gstate.isClearModeColorMask())
			keepOldMask &= 0x0000F000;
		if (gstate.isClearModeAlphaMask())
			keepOldMask &= 0x00000FFF;
		break;

	case GE_FORMAT_8888:
		if (gstate.isClearModeColorMask())
			keepOldMask &= 0xFF000000;
		if (gstate.isClearModeAlphaMask())
			keepOldMask &= 0x00FFFFFF;
		break;

	case GE_FORMAT_INVALID:
		_dbg_assert_msg_(G3D, false, "Software: invalid framebuf format.");
		break;
	}


	if (keepOldMask == 0) {
		ScreenCoords pprime(minX, minY, 0);
		const int stride = gstate.FrameBufStride();

		if (gstate.FrameBufFormat() == GE_FORMAT_8888) {
			for (pprime.y = minY; pprime.y < maxY; pprime.y += 16) {
				DrawingCoords p = TransformUnit::ScreenToDrawing(pprime);
				if ((new_color & 0xFF) == (u8)(new_color >> 8) && (new_color & 0xFFFF) == (new_color >> 16)) {
					#if PPSSPP_PLATFORM(WII)
          Wii::FillBuffer(fb.data,p.x,p.y,stride,4,w,uint8_t(new_color));
#else
          void *row = &fb.as32[p.x + p.y * stride];
          memset(row,new_color,w*4);
#endif
				} else {
					for (int x = 0; x < w; ++x) {
						fb.Set32(p.x + x, p.y, stride, new_color);
					}
				}
			}
		} else {
			for (pprime.y = minY; pprime.y < maxY; pprime.y += 16) {
				DrawingCoords p = TransformUnit::ScreenToDrawing(pprime);
				if ((new_color16 & 0xFF) == (new_color16 >> 8)) {
					#if PPSSPP_PLATFORM(WII)
          Wii::FillBuffer(fb.data,p.x,p.y,stride,2,w,uint8_t(new_color16));
#else
          void *row = &fb.as16[p.x + p.y * stride];
          memset(row,new_color16,w*2);
#endif
				} else {
					for (int x = 0; x < w; ++x) {
						fb.Set16(p.x + x, p.y, stride, new_color16);
					}
				}
			}
		}
	} else if (keepOldMask != 0xFFFFFFFF) {
		ScreenCoords pprime(minX, minY, 0);
		const int stride = gstate.FrameBufStride();

		if (gstate.FrameBufFormat() == GE_FORMAT_8888) {
			for (pprime.y = minY; pprime.y < maxY; pprime.y += 16) {
				DrawingCoords p = TransformUnit::ScreenToDrawing(pprime);
				for (int x = 0; x < w; ++x) {
					const u32 old_color = fb.Get32(p.x + x, p.y, stride);
					const u32 c = (old_color & keepOldMask) | (new_color & ~keepOldMask);
					fb.Set32(p.x + x, p.y, stride, c);
				}
			}
		} else {
			for (pprime.y = minY; pprime.y < maxY; pprime.y += 16) {
				DrawingCoords p = TransformUnit::ScreenToDrawing(pprime);
				for (int x = 0; x < w; ++x) {
					const u16 old_color = fb.Get16(p.x + x, p.y, stride);
					const u16 c = (old_color & keepOldMask) | (new_color16 & ~keepOldMask);
					fb.Set16(p.x + x, p.y, stride, c);
				}
			}
		}
	}
}
