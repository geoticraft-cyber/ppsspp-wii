// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstring>
namespace Wii {
// One-entry, batch-local derived state. Reset before any new DecodeVerts call;
// never reuse across bone-register changes, even if the weights are identical.
class SkinMatrixMemo {
  float weights_[8],matrix_[12];unsigned count_=0;
public:
  void Reset(){count_=0;}
  bool Load(const float *weights,unsigned count,float *matrix)const {
    if(!count||count!=count_||std::memcmp(weights_,weights,count*4))return false;
    std::memcpy(matrix,matrix_,sizeof(matrix_));return true;
  }
  void Store(const float *weights,unsigned count,const float *matrix){
    if(!count||count>8){Reset();return;}
    std::memcpy(weights_,weights,count*4);std::memcpy(matrix_,matrix,sizeof(matrix_));count_=count;
  }
};
}
