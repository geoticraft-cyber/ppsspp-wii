#include <cassert>
#include "Wii/MenuLayout.h"
int main() {
  for (unsigned i=0;i<Wii::MenuRows;++i) {
    auto r=Wii::GameRow(i);
    assert(r.Contains(r.x,r.y)); assert(r.Contains(r.x+r.w-1,r.y+r.h-1));
    assert(!r.Contains(r.x+r.w,r.y)); assert(!r.Contains(r.x,r.y+r.h));
    for (unsigned j=0;j<Wii::MenuRows;++j)
      assert(Wii::GameRow(j).Contains(r.x+20,r.y+16)==(i==j));
    for (unsigned j=0;j<6;++j) assert(!Wii::MenuButton(j).Contains(r.x+20,r.y+16));
  }
  for(unsigned i=0;i<6;++i) for(unsigned j=0;j<6;++j) {
    auto r=Wii::MenuButton(i);
    assert(Wii::MenuButton(j).Contains(r.x+20,r.y+16)==(i==j));
  }
  assert(Wii::CPUButton().Contains(350,30));
  assert(!Wii::CPUButton().Contains(470,30));
  assert(Wii::GXButton().Contains(470,30));
  assert(!Wii::ExitButton().Contains(470,30));
  assert(!Wii::SourceButton(0).Contains(100,80));
  assert(Wii::SourceButton(1).Contains(100,80));
}
