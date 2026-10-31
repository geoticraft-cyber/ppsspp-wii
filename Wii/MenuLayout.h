#pragma once
namespace Wii {
struct MenuRect {
  int x,y,w,h;
  bool Contains(int px,int py) const { return px>=x && px<x+w && py>=y && py<y+h; }
};
constexpr unsigned MenuRows = 6;
inline MenuRect GameRow(unsigned i) { return {28+int(i%3)*198,106+int(i/3)*115,188,105}; }
inline MenuRect MenuButton(unsigned i) { return {24+int(i)*100,398,92,32}; }
inline MenuRect GXButton() { return {442,20,100,30}; }
inline MenuRect SourceButton(unsigned i) { return {24+int(i)*66,72,58,26}; }
inline MenuRect ExitButton() { return {550,20,66,30}; }
inline MenuRect DialogButton(unsigned i) { return {154+int(i)*178,342,154,34}; }
}
