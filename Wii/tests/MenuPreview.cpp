#include "Wii/Menu.h"
#include "Wii/Video.h"
#include <algorithm>
#include <cstdio>
#include <vector>
#include "Wii/MenuFont.h"
static unsigned pixels[640*480];
void WiiMenuBegin() { std::fill(pixels,pixels+640*480,0xe7eef4); }
void WiiMenuRect(int x,int y,int w,int h,unsigned rgb) {
  for(int j=std::max(y,0);j<std::min(y+h,480);++j)
    for(int i=std::max(x,0);i<std::min(x+w,640);++i) pixels[j*640+i]=rgb;
}
void WiiMenuText(int x,int y,const char *text,unsigned rgb,int scale) {
  for(const unsigned char *p=(const unsigned char *)text;*p;++p,x+=8*scale) {
    unsigned c=*p>=32&&*p<127?*p:'?';
    for(int j=0;j<16;++j) for(int i=0;i<8;++i)
      if(menuFont[c][j]&(128>>i)) WiiMenuRect(x+i*scale,y+j*scale,scale,scale,rgb);
  }
}
void WiiMenuEnd() {}
int main(int argc,char **argv) {
  std::vector<Wii::DiscImage> games={
    {"PSP-smoke.iso","sd:/disks/PSP-smoke.iso",123456},
    {"Adventure Homebrew.cso","sd:/disks/Homebrew/Adventure Homebrew.cso",45678912},
    {"Another PSP Homebrew.iso","sd:/disks/Another PSP Homebrew.iso",78901234},
    {"Puzzle.cso","sd:/disks/Puzzle.cso",12567890},
    {"Racing Homebrew.ISO","sd:/disks/Racing Homebrew.ISO",234567890},
    {"Very long homebrew filename.cso","sd:/disks/Very long homebrew filename.cso",7777777}};
  Wii::ScanReport report;report.folderReadable=true;report.path="sd:/disks/";
  report.entries=10;report.folders=2;report.candidates=6;
  Wii::MenuView v{};v.games=&games;v.report=&report;v.cardInserted=v.rootReadable=true;
  v.status="SD ready. Select a game.";
  v.rootSummary="Root contains: apps, disks, bootmii, private";
  v.pointer=true;v.px=275;v.py=258;v.hover=4;
  if(argc>2 && argv[2][0]=='i') { v.info=true; }
  if(argc>2 && argv[2][0]=='c') { v.confirm=true; }
  if(argc>2 && argv[2][0]=='e') {games.clear();report.folderReadable=false;report.candidates=0;v.scanError="Cannot open sd:/disks/: No such file or directory";}
  Wii::DrawMenu(v);
  FILE *f=fopen(argv[1],"wb");fprintf(f,"P6\n640 480\n255\n");
  for(unsigned c:pixels) { unsigned char rgb[]={ (unsigned char)(c>>16),(unsigned char)(c>>8),(unsigned char)c};fwrite(rgb,1,3,f); }fclose(f);
}
