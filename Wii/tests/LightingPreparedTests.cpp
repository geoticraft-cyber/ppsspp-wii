// SPDX-License-Identifier: GPL-2.0-or-later
#include "GPU/Software/Lighting.h"
#include "GPU/GPUState.h"
#include <cassert>
#include <cstring>
#include <cstdio>
GPUgstate gstate{};
GPUStateCache gstate_c{};
static uint32_t seed=0x321abc;
static uint32_t Random() {seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static uint32_t F24(float value) {uint32_t bits;memcpy(&bits,&value,4);return bits>>8;}
int main() {
  for(unsigned trial=0;trial<30000;++trial) {
    gstate.lightingEnable=trial&1;gstate.materialupdate=Random()&7;gstate.lmode=Random()&1;
    gstate.materialambient=Random();gstate.materialdiffuse=Random();gstate.materialspecular=Random();gstate.materialemissive=Random();
    gstate.ambientcolor=Random();gstate.ambientalpha=Random()&255;gstate.materialalpha=Random()&255;
    gstate.materialspecularcoef=F24(float(Random()%5));
    gstate.texmapmode=(trial/2)%3;gstate.texshade=(Random()&3)|((Random()&3)<<8);
    for(unsigned i=0;i<4;++i) {
      gstate.lightEnable[i]=Random()&1;gstate.ltype[i]=(Random()%3)|((Random()%4)<<8);
      for(unsigned j=0;j<3;++j) {
        gstate.lpos[i*3+j]=F24((int(Random()%17)-8)*0.25f+(j==2?1.f:0.f));
        gstate.ldir[i*3+j]=F24((int(Random()%17)-8)*0.25f+(j==2?1.f:0.f));
        gstate.latt[i*3+j]=F24(j==0?1.f:0.125f);
        gstate.lcolor[i*3+j]=Random()&0xffffff;
      }
      // Keep normalization defined, also test zero/non-positive dot powers.
      gstate.lpos[i*3+2]=F24(2.f);gstate.ldir[i*3+2]=F24(1.f);
      gstate.lcutoff[i]=F24(-0.5f);gstate.lconv[i]=F24(float(Random()%4));
    }
    Lighting::PreparedState prepared;Lighting::Prepare(prepared);
    for(unsigned v=0;v<4;++v) {
      VertexData actual{},reference{};
      actual.worldpos=Vec3<float>((int(Random()%9)-4)*0.5f,0.f,0.f);
      actual.worldnormal=Vec3<float>(0.25f,0.25f,1.f).Normalized();
      actual.color0=Vec4<int>(Random()%256,Random()%256,Random()%256,Random()%256);
      actual.color1=Vec3<int>(1,2,3);actual.texturecoords=Vec2<float>(0.3f,0.7f);
      reference=actual;
      Lighting::Process(reference,v&1);Lighting::ProcessPrepared(actual,v&1,prepared);
      assert(!memcmp(&actual,&reference,sizeof(actual)));
    }
  }
  puts("pass: 120000 prepared/reference lighting vertices, materials, all light types, env-map, alpha and secondary color, state changes each draw");
}
