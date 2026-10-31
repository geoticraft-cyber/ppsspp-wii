// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
bool WiiVideoInit();
// RECOVERED_1_2: select/apply the configured VI mode (0 Auto, 1 480i, 2 480p, 3 removed, 4 576p).
bool WiiVideoApplyMode(int video);
bool WiiVideoModeSupported(int video);
bool WiiVideoPresent();
void WiiVideoReport();
void WiiVideoShutdown();
void WiiMenuBegin();
void WiiMenuRect(int x, int y, int w, int h, unsigned rgb);
void WiiMenuText(int x, int y, const char *text, unsigned rgb, int scale = 1);
void WiiMenuEnd();
