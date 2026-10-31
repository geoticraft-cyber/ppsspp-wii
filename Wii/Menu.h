#pragma once
#include "Wii/DiscBrowser.h"
#include "Wii/MenuLayout.h"
namespace Wii {
struct MenuView {
  const std::vector<DiscImage> *games;
  const ScanReport *report;
  size_t page=0,selected=0;
  int hover=-1;
  bool cardInserted=false,rootReadable=false;
  bool info=false,confirm=false;
  bool usb=false,gxEnabled=true;
  std::string status,rootSummary,scanError;
  bool pointer=false;
  int px=320,py=240;
};
void DrawMenu(const MenuView &v);
void DrawMessage(const char *title,const std::string &message);
}
