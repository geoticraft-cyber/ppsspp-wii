#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise actual primitive assembly across draws and separate GPU instances."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'GPU/Software/TransformUnit.cpp').read_text();a=s.index('\tconst int max_vtcs_per_prim');b=s.index('  auto &work=Wii::GetGraphicsWork();\n  work.vertexRequests',a)
assembly=s[a:b].replace('#if PPSSPP_PLATFORM(WII)\n  // Keep', '// Keep')
# The final #if starts before the counter normalization; close it locally.
if assembly.count('#if')!=assembly.count('#endif'):assembly+='\n#endif\n'
h=(root/'GPU/Software/TransformUnit.h').read_text();m=h.index('    VertexData primitiveVertices');n=h.index('\n#endif',m)
code=r'''
#include <cassert>
#include <cstdio>
#include <vector>
#include <initializer_list>
#define PPSSPP_PLATFORM(x) 1
#define _dbg_assert_msg_(tag,c,...) assert(c)
#define ERROR_LOG(...) assert(false)
enum GEPrimitiveType{GE_PRIM_POINTS,GE_PRIM_LINES,GE_PRIM_LINE_STRIP,GE_PRIM_TRIANGLES,GE_PRIM_TRIANGLE_STRIP,GE_PRIM_TRIANGLE_FAN,GE_PRIM_RECTANGLES,GE_PRIM_KEEP_PREVIOUS};
struct VertexData{int id;explicit VertexData(int value=0):id(value){}};struct Input{int id;bool outside;Input(int value,bool flag=false):id(value),outside(flag){}};
static std::vector<std::vector<int>> output;
struct {bool isCullEnabled(){return true;}bool isModeClear(){return false;}bool getCullMode(){return true;}}gstate;
namespace Clipper {
void ProcessTriangle(VertexData&a,VertexData&b,VertexData&c){output.push_back({a.id,b.id,c.id});}
void ProcessRect(VertexData&a,VertexData&b){output.push_back({a.id,b.id});}
void ProcessLine(VertexData&a,VertexData&b){output.push_back({a.id,b.id});}
void ProcessPoint(VertexData&a){output.push_back({a.id});}
}
struct Transform {
 bool outside_range_flag=false;
'''+h[m:n]+r'''
 void Submit(GEPrimitiveType prim_type,std::initializer_list<Input> list) {
  const std::vector<Input> input(list);const int vertex_count=input.size();
  const auto readVertex=[&](int index) {outside_range_flag=outside_range_flag||input[index].outside;return VertexData{input[index].id};};
'''+assembly+r'''
 }
};
int main(){
 Transform a;output.clear();a.Submit(GE_PRIM_TRIANGLES,{{9,true}});assert(output.empty()&&a.outside_range_flag);
 a.Submit(GE_PRIM_TRIANGLES,{{1},{2},{3}});assert((output==std::vector<std::vector<int>>{{1,2,3}}));
 output.clear();a.Submit(GE_PRIM_TRIANGLES,{{4},{5}});a.Submit(GE_PRIM_KEEP_PREVIOUS,{{6}});
 assert((output==std::vector<std::vector<int>>{{4,5,6}}));
 output.clear();a.Submit(GE_PRIM_TRIANGLES,{{4,true},{5}});a.Submit(GE_PRIM_KEEP_PREVIOUS,{{6}});assert(output.empty());
 // Partial vertices from a different GPU/game cannot complete this triangle.
 Transform b;output.clear();a.Submit(GE_PRIM_TRIANGLES,{{91},{92}});b.Submit(GE_PRIM_KEEP_PREVIOUS,{{7}});
 assert((output==std::vector<std::vector<int>>{{7}}));
 output.clear();b.Submit(GE_PRIM_TRIANGLES,{{81},{82}});a.Submit(GE_PRIM_KEEP_PREVIOUS,{{93}});b.Submit(GE_PRIM_KEEP_PREVIOUS,{{83}});
 assert((output==std::vector<std::vector<int>>{{91,92,93},{81,82,83}}));
 output.clear();Transform strip;strip.Submit(GE_PRIM_TRIANGLE_STRIP,{{0},{1},{2}});
 for(int i=3;i<10000;++i)strip.Submit(GE_PRIM_KEEP_PREVIOUS,{{i}});
 assert(output.size()==9998&&strip.primitiveIndex<12);
 for(unsigned i=0;i<output.size();++i){auto triangle=output[i];int sum=0;for(int v:triangle){assert(v>=int(i)&&v<=int(i+2));sum+=v;}assert(sum==int(i*3+3));}
 puts("pass: actual primitive assembly: new-draw culling reset, KEEP continuity, separate games/instances, interleaving, and 10000-vertex strip counter bounds");
}
'''
with tempfile.TemporaryDirectory(prefix='primitive-assembly-',dir=Path.cwd()) as t:
 p=Path(t);cpp=p/'test.cpp';cpp.write_text(code);exe=p/'test'
 subprocess.run(['g++','-std=c++11','-O2','-fsanitize=undefined','-fno-sanitize-recover=all',str(cpp),'-o',str(exe)],check=True)
 subprocess.run([str(exe.resolve())],check=True)
