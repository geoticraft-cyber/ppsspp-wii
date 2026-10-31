// SPDX-License-Identifier: GPL-2.0-or-later
#include <cstdio>
#include <cstring>
#include "base/NativeApp.h"
#include "Core/Core.h"
#include "Core/System.h"
#include "gfx_es2/gpu_features.h"

GLExtensions gl_extensions{};
void ShaderTranslationInit() {}
void ShaderTranslationShutdown() {}
void NativeUpdate() {}
void NativeRender(GraphicsContext *) {}
void NativeResized() {}
void NativeMessageReceived(const char *, const char *) {}
void System_SendMessage(const char *command, const char *) {
  if (!strcmp(command, "finish")) Core_Stop();
}
std::string System_GetProperty(SystemProperty prop) {
  if (prop == SYSPROP_NAME) return "Nintendo Wii";
  if (prop == SYSPROP_CPUINFO) return "Broadway PowerPC 750";
  if (prop == SYSPROP_LANGREGION) return "en_US";
  return "";
}
int System_GetPropertyInt(SystemProperty prop) {
  switch (prop) {
  case SYSPROP_DEVICE_TYPE: return DEVICE_TYPE_TV;
  case SYSPROP_DISPLAY_XRES: return 640;
  case SYSPROP_DISPLAY_YRES: return 480;
  case SYSPROP_DISPLAY_REFRESH_RATE: return 60000;
  case SYSPROP_DISPLAY_DPI: return 96;
  case SYSPROP_DISPLAY_COUNT: return 1;
  default: return -1;
  }
}
bool System_GetPropertyBool(SystemProperty) { return false; }
void System_AskForPermission(SystemPermission) {}
PermissionStatus System_GetPermissionStatus(SystemPermission) { return PERMISSION_STATUS_GRANTED; }
bool System_InputBoxGetWString(const wchar_t *, const std::wstring &, std::wstring &) { return false; }
void SystemToast(const char *text) { printf("%s\n", text); }
void ShowKeyboard() {}
void Vibrate(int) {}
void LaunchBrowser(const char *) {}
void LaunchMarket(const char *) {}
void LaunchEmail(const char *) {}
