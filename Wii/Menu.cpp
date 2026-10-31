#include "Wii/Menu.h"
#include "Wii/Video.h"
#include <algorithm>
#include <cstdio>
namespace Wii {
static void Panel(MenuRect r, unsigned fill, unsigned edge) {
  WiiMenuRect(r.x+2,r.y+3,r.w,r.h,0xdac4c4);
  WiiMenuRect(r.x,r.y,r.w,r.h,edge);
  WiiMenuRect(r.x+2,r.y+2,r.w-4,r.h-4,fill);
}
static void Text(int x,int y,const std::string &s,unsigned color=0x5c2525) {
  WiiMenuText(x,y,s.c_str(),color);
}
static void Lines(int x,int y,const std::string &s,size_t width,unsigned count,unsigned color=0x5c2525) {
  for(unsigned i=0;i<count && i*width<s.size();++i)
    Text(x,y+int(i)*19,DisplayFilename(s.substr(i*width),width),color);
}
void DrawMessage(const char *title,const std::string &message) {
  WiiMenuBegin();
  Panel({40,128,560,192},0xffffff,0xc8a5a5);
  WiiMenuText(64,152,title,0x8c1414,2);
  Lines(64,208,message,62,4);
  WiiMenuEnd();
}
void DrawMenu(const MenuView &v) {
  const auto &games=*v.games;
  const size_t pages=std::max(size_t(1),(games.size()+5)/6);
  WiiMenuBegin();
  for(int y=0;y<480;y+=8) {
    unsigned c=unsigned(239-y/24); WiiMenuRect(0,y,640,8,(c<<16)|((c+5)<<8)|(c+10));
  }
  WiiMenuRect(0,0,640,66,0xffffff);
  WiiMenuRect(0,64,640,3,0xd44343);
  WiiMenuText(26,16,"PPSSPP",0x981212,2);
  Text(226,22,"Wii",0x835353);
  Panel(GXButton(),v.gxEnabled?0xf8cccc:0xffffff,0xcf9f9f);
  Text(452,28,v.gxEnabled?"GX: Auto":"GX: Off");
  Panel(ExitButton(),v.hover==19?0xfbd6d6:0xfaf3f3,0xd8b5b5);
  Text(566,28,"Exit");
  WiiMenuRect(24,78,10,10,v.cardInserted&&v.rootReadable?0x32a47a:0xdc725a);
  for(unsigned i=0;i<2;++i) { auto r=SourceButton(i);
    Panel(r,(v.usb==(i==1))?0xf8cccc:0xffffff,0xcf9f9f);
    Text(r.x+14,r.y+6,i?"USB":"SD");
  }
  Text(158,77,DisplayFilename(v.report->path,28),0x753b3b);
  char label[100];
  snprintf(label,sizeof(label),"%u games  |  %u/%u",unsigned(games.size()),unsigned(v.page+1),unsigned(pages));
  Text(412,75,label,0x835353);
  for(unsigned i=0;i<6 && v.page*6+i<games.size();++i) {
    size_t index=v.page*6+i; const auto &g=games[index]; auto r=GameRow(i);
    bool active=index==v.selected, hover=v.hover==int(i);
    Panel(r,0xffffff,hover?0xdc2d2d:active?0xa81515:0xd6b6b6);
    WiiMenuRect(r.x+8,r.y+8,r.w-16,44,active?0xf8cccc:0xf7e7e7);
    // Original PSP silhouette; no downloaded cover art required.
    WiiMenuRect(r.x+49,r.y+17,88,24,0x774747);
    WiiMenuRect(r.x+65,r.y+20,55,18,0xd29191);
    WiiMenuRect(r.x+54,r.y+26,9,3,0xfae6e6);
    WiiMenuRect(r.x+57,r.y+23,3,9,0xfae6e6);
    WiiMenuRect(r.x+127,r.y+26,4,4,0xfae6e6);
    std::string title=g.name.substr(0,g.name.size()-4);
    Text(r.x+10,r.y+58,DisplayFilename(title,21),0x5f2121);
    const std::string ext=g.name.substr(g.name.size()-3);
    if (g.size >= 1024*1024)
      snprintf(label,sizeof(label),"%s   %llu MB",ext.c_str(),static_cast<unsigned long long>(g.size/(1024*1024)));
    else if (g.size)
      snprintf(label,sizeof(label),"%s   %llu KB",ext.c_str(),static_cast<unsigned long long>((g.size+1023)/1024));
    else snprintf(label,sizeof(label),"%s   size unknown",ext.c_str());
    Text(r.x+10,r.y+80,label,0x986969);
  }
  if(games.empty()) {
    Panel({28,110,584,208},0xffffff,0xd6b6b6);
    WiiMenuText(50,130,v.report->folderReadable?"No disc images here":"Cannot read game folder",0x912424,2);
    Lines(50,178,!v.scanError.empty()?v.scanError:v.report->Summary(),65,2);
    Text(50,226,"Rescan retries this folder.");
    Text(50,252,"Find searches this device for ISO/CSO.");
    Text(50,280,"Info shows the actual device contents.",0x835353);
  }
  const std::string selected=games.empty()?"Select Info to inspect this device.":games[v.selected].name;
  Text(28,344,DisplayFilename(selected,72),0x781d1d);
  Text(28,370,DisplayFilename(v.status.empty()?v.report->Summary():v.status,72),0x835353);
  const char *buttons[]={"Play","< Prev","Next >","Rescan","Find","Info"};
  for(unsigned i=0;i<6;++i) {
    auto r=MenuButton(i); bool hover=v.hover==10+int(i);
    Panel(r,hover?0xfbd2d2:i==0?0xfcd8d8:0xffffff,hover?0xc82020:0xcea5a5);
    Text(r.x+10,r.y+9,buttons[i],i==0&&games.empty()?0xb28a8a:0x7a2020);
  }
  Text(26,447,"Point + A: select   D-pad: move   A/2: play",0x855252);
  Text(26,465,"B: back   Minus/GC X: settings   HOME: exit",0x986969);
  if(v.info || v.confirm) {
    // Opaque modal keeps errors and confirmation stable instead of flashing consoles.
    Panel({38,98,564,292},0xffffff,0xbb5252);
    WiiMenuText(58,115,v.info?"Device information":"Launch PSP game",0x8e1616,2);
    if(v.info) {
      Text(58,160,v.cardInserted?"Device: detected":"Device: not detected",v.cardInserted?0x238367:0xb04632);
      Text(58,182,v.rootReadable?"Filesystem: device root is readable":"Filesystem: device root cannot be read");
      Lines(58,207,v.rootSummary,64,2);
      Lines(58,251,v.report->path+" | "+v.report->Summary(),64,2);
      std::string issue=!v.scanError.empty()?v.scanError:
        !v.report->problems.empty()?v.report->problems[0]:"No directory read errors reported.";
      Lines(58,293,issue,64,2,0xa56a25);
    } else if(!games.empty()) {
      Lines(58,166,games[v.selected].name,62,2);
      Lines(58,214,games[v.selected].path,62,3,0x956565);
      Lines(58,282,v.status.empty()?"Interpreter / software GPU":v.status,62,2,0xa56a25);
    }
    for(unsigned i=0;i<2;++i) {
      auto r=DialogButton(i);Panel(r,v.hover==20+int(i)?0xfbc9c9:0xf8e6e6,0xca7979);
      Text(r.x+20,r.y+9,v.info?(i?"Close":"Find device"):(i?"Back":"Launch"));
    }
  }
  if(v.pointer && v.px>=0 && v.py>=0) {
    for(int y=0;y<18;++y) WiiMenuRect(v.px,v.py+y,2+y/2,1,0x601414);
    for(int y=1;y<14;++y) WiiMenuRect(v.px+2,v.py+y,1+y/2,1,0xffffff);
  }
  WiiMenuEnd();
}
}
