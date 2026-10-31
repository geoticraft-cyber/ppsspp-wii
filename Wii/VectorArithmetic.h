// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Wii {
// Inputs are already gathered and prefixed by the VFPU handler. Output masks
// and saturation remain in the handler. No fused arithmetic or reassociation.
template<unsigned Operation>
// Keep these tiny loops out of the -O3 handler to avoid duplicated unrolling.
#if defined(__GNUC__) && !defined(__clang__)
__attribute__((noinline,optimize("Os")))
#endif
void VectorArithmetic(float *d,const float *s,const float *t,unsigned n) {
  unsigned i=0;
#if defined(WII_PAIRED_SINGLE) && defined(__powerpc__) && !defined(PPSSPP_WII_PORTABLE_TEST)
  // GQR0 is normally float/no-scale in libogc. Check rather than modifying a
  // shared quantization register. Other formats retain scalar execution.
  unsigned gqr0;
  __asm__ volatile("mfspr %0,912" : "=r"(gqr0));
  if(Operation<3 && n>=2 && gqr0==0) {
    for(;i+1<n;i+=2) {
      // Explicit scratch FPR clobbers let GCC preserve live scalar values.
      // Load both sources before storing, permitting exact input/output alias.
#define WII_PS_BINARY(instruction) \
      __asm__ volatile( \
        "psq_l 0,0(%1),0,0\n\t" \
        "psq_l 1,0(%2),0,0\n\t" \
        instruction " 0,0,1\n\t" \
        "psq_st 0,0(%0),0,0" \
        : : "b"(d+i),"b"(s+i),"b"(t+i) : "fr0","fr1","memory")
      if(Operation==0) {WII_PS_BINARY("ps_add");}
      if(Operation==1) {WII_PS_BINARY("ps_sub");}
      if(Operation==2) {WII_PS_BINARY("ps_mul");}
#undef WII_PS_BINARY
    }
  }
#endif
  for(;i<n;++i) {
    if(Operation==0)d[i]=s[i]+t[i];
    if(Operation==1)d[i]=s[i]-t[i];
    if(Operation==2)d[i]=s[i]*t[i];
    if(Operation==3)d[i]=s[i]/t[i];
  }
}
}
