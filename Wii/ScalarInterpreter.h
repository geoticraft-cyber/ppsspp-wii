#pragma once
#include <cstdint>
namespace Wii {
// No branch, exception, memory or VFPU semantics are changed. Unsupported
// encodings retain PPSSPP's normal decoder.
inline bool InterpretScalarOther(uint32_t op,uint32_t *r,uint32_t &pc) {
  unsigned code=op>>26,rs=(op>>21)&31,rt=(op>>16)&31,rd=(op>>11)&31,sa=(op>>6)&31;
  uint32_t result;unsigned dest=rt;
  switch(code) {
    case 9:result=r[rs]+uint32_t(int32_t(int16_t(op)));break;
    case 10:result=int32_t(r[rs])<int32_t(int16_t(op));break;
    case 11:result=r[rs]<uint32_t(int32_t(int16_t(op)));break;
    case 12:result=r[rs]&(op&65535);break;
    case 13:result=r[rs]|(op&65535);break;
    case 14:result=r[rs]^(op&65535);break;
    case 15:if(rs)return false;result=(op&65535)<<16;break;
    case 0:
      dest=rd;
      switch(op&63) {
        case 0:if(rs)return false;result=r[rt]<<sa;break;
        case 2:if(rs>1)return false;result=rs?((r[rt]>>sa)|(r[rt]<<((32-sa)&31))):r[rt]>>sa;break;
        case 3:if(rs)return false;result=uint32_t(int32_t(r[rt])>>sa);break;
        case 4:if(sa)return false;result=r[rt]<<(r[rs]&31);break;
        case 6:if(sa>1)return false;result=sa?((r[rt]>>(r[rs]&31))|(r[rt]<<((32-(r[rs]&31))&31))):r[rt]>>(r[rs]&31);break;
        case 7:if(sa)return false;result=uint32_t(int32_t(r[rt])>>(r[rs]&31));break;
        case 10:if(sa)return false;result=r[rt]==0?r[rs]:r[rd];break;
        case 11:if(sa)return false;result=r[rt]!=0?r[rs]:r[rd];break;
        case 22:case 23: {
          if(sa||rt)return false;
          const uint32_t value=(op&63)==22?r[rs]:~r[rs];
          result=value?unsigned(__builtin_clz(value)):32;break;
        }
        case 33:if(sa)return false;result=r[rs]+r[rt];break;
        case 35:if(sa)return false;result=r[rs]-r[rt];break;
        case 36:if(sa)return false;result=r[rs]&r[rt];break;
        case 37:if(sa)return false;result=r[rs]|r[rt];break;
        case 38:if(sa)return false;result=r[rs]^r[rt];break;
        case 39:if(sa)return false;result=~(r[rs]|r[rt]);break;
        case 42:if(sa)return false;result=int32_t(r[rs])<int32_t(r[rt]);break;
        case 43:if(sa)return false;result=r[rs]<r[rt];break;
        default:return false;
      }
      break;
    case 31:
      if((op&63)==0) {  // EXT: size is rd+1; avoid shifts by 32.
        if(sa+rd+1>32)return false;
        result=(r[rs]>>sa)&(0xffffffffu>>(31-rd));
      } else if((op&63)==4) {  // INS: rd is the last destination bit.
        if(rd<sa)return false;
        const uint32_t mask=(0xffffffffu>>(31-(rd-sa)))<<sa;
        result=(r[rt]&~mask)|((r[rs]<<sa)&mask);
      } else {
        if(rs||(op&63)!=32)return false;
        dest=rd;
        switch(sa) {
          case 2:result=((r[rt]&0xff00ff00u)>>8)|((r[rt]&0x00ff00ffu)<<8);break;
          case 3:result=__builtin_bswap32(r[rt]);break;
          case 16:result=uint32_t(int32_t(int8_t(r[rt])));break;
          case 24:result=uint32_t(int32_t(int16_t(r[rt])));break;
          default:return false;
        }
      }
      break;
    default:return false;
  }
  if(dest)r[dest]=result;
  pc+=4;return true;
}
// Keep the common stack/address/loop increment path small enough to inline.
inline bool InterpretScalar(uint32_t op,uint32_t *r,uint32_t &pc) {
  const unsigned kind=op>>26,rt=(op>>16)&31,rs=(op>>21)&31;
  if(kind==9) {
    if(rt)r[rt]=r[rs]+uint32_t(int32_t(int16_t(op)));
    pc+=4;return true;
  }
  // Keep frequent address/mask/constant operations in the caller's hot loop.
  if(kind>=12&&kind<=15) {
    if(kind==15&&rs)return false;
    const uint32_t imm=op&65535;
    uint32_t value=0;
    if(kind==12)value=r[rs]&imm;
    if(kind==13)value=r[rs]|imm;
    if(kind==14)value=r[rs]^imm;
    if(kind==15)value=imm<<16;
    if(rt)r[rt]=value;
    pc+=4;return true;
  }
  return InterpretScalarOther(op,r,pc);
}
// Separate HI/LO references keep the scalar helper safe for 32-register arrays.
inline bool InterpretHiLo(uint32_t op,uint32_t *r,uint32_t &lo,uint32_t &hi,uint32_t &pc) {
  if(op>>26)return false;
  const unsigned rs=(op>>21)&31,rt=(op>>16)&31,rd=(op>>11)&31,sa=(op>>6)&31,fn=op&63;
  if(sa)return false;
  switch(fn) {
    case 16:case 18:
      if(rs||rt)return false;
      if(rd)r[rd]=fn==16?hi:lo;break;
    case 17:case 19:
      if(rt||rd)return false;
      if(fn==17)hi=r[rs];else lo=r[rs];break;
    case 24:case 25:case 28:case 29:case 46:case 47: {
      if(rd)return false;
      // Keep the product in two native words. Early-clobber protects inputs
      // needed by the second instruction; do not pin guest registers globally.
      uint32_t productLo,productHi;
#if defined(__powerpc__) && !defined(PPSSPP_WII_PORTABLE_TEST)
      const uint32_t lhs=r[rs],rhs=r[rt];
      if(fn&1) {
        __asm__("mullw %0,%2,%3\n\tmulhwu %1,%2,%3"
          : "=&r"(productLo),"=&r"(productHi) : "r"(lhs),"r"(rhs));
      } else {
        __asm__("mullw %0,%2,%3\n\tmulhw %1,%2,%3"
          : "=&r"(productLo),"=&r"(productHi) : "r"(lhs),"r"(rhs));
      }
#else
      const uint64_t product=(fn&1)==0?uint64_t(int64_t(int32_t(r[rs]))*int64_t(int32_t(r[rt]))):uint64_t(r[rs])*r[rt];
      productLo=uint32_t(product);productHi=uint32_t(product>>32);
#endif
      const uint32_t oldLo=lo;
      if(fn>=46) {
        lo=oldLo-productLo;hi=hi-productHi-uint32_t(oldLo<productLo);
      } else if(fn>=28) {
        lo=oldLo+productLo;hi=hi+productHi+uint32_t(lo<oldLo);
      } else {lo=productLo;hi=productHi;}
      break;
    }
    default:return false;
  }
  pc+=4;return true;
}

}
