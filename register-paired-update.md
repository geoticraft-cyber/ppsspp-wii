# register and vector arithmetic update

this is an updated source distribution, not a tested wii executable.

## changes

- integer multiply/accumulate: use explicit low/high 32-bit words, unsigned carry and borrow, and native powerpc mullw/mulhw/mulhwu instructions on target. compiler constraints allocate scratch registers without reserving guest registers or changing the abi. the portable path keeps a 64-bit product as an independent way to check the word arithmetic.
- vfpu add/subtract/multiply/divide: decode the operation once per vector. share four small, size-optimized arithmetic helpers rather than duplicating unrolled loops inside the speed-optimized handler. source prefixes, destination saturation/masks, pc updates, and prefix consumption remain in the original handler.
- experimental paired-single backend: two lanes per ps_add/ps_sub/ps_mul operation. an odd final lane and division use scalar arithmetic. paired loads/stores require gqr0=0; otherwise the helper falls back to scalar code. it does not modify gqr0. explicit fpr clobbers and a memory clobber protect compiler-managed values. no fused multiply-add or matrix reassociation is introduced.
- the paired backend is off by default. enable with `python3 Wii/build_release.py --paired-single` or `-DWII_PAIRED_SINGLE=ON` when configuring the wii cmake build. omit the flag for the default scalar build.

## validation completed

- full portable core: compiled and linked, then passed io, smoke, dispatch, services, and guest-waits checks using `Wii/tests/run_cpu_performance_checks.py`.
- 100,000 hi/lo cases against original handlers and an independent modular arithmetic oracle, including signed overflow edges.
- 120,000 scalar bitfield/byte-order cases against original handlers.
- 480,000 vector arithmetic comparisons against the supplied original handler: sizes 1–4, source/destination overlap, prefix call sequencing, write masks, signed zero, subnormal, infinity, and nan inputs. finite results and signed zeros are compared by bits; nan results by classification because compiler operand selection can change payload propagation. this test uses lightweight register/prefix scaffolding, not a complete psp vfpu model.
- 65,536 existing actual vector read/write comparisons, including raw ieee payloads and masks.
- 400,000 existing timing-expression comparisons.
- build-release and new test python syntax checked.

## size evidence and limitations

with the host g++ compiler and the actual core source at -O3, the changed vector handler plus its four arithmetic helpers shrank from 1,131 to 694 bytes, a reduction of 437 bytes (38.6%). the entire vfpu object text reported by `size` decreased from 36,701 to 36,376 bytes. object-file disk size increased slightly because separate functions add symbols/section metadata. these are host measurements, not the wii dol size.

host microbenchmark timings were noisy and did not establish a speed improvement. no gameplay fps claim is made. the source tree retains other platform sources, assets, and regression tests; deleting them would not establish an executable performance improvement.

devkitppc/libogc2 and wii hardware were not available here. the powerpc inline assembly and paired-single backend have not been target-compiled or run. verify both on real hardware before distributing a binary. in particular, compare rounding modes, subnormals, nan behavior, and floating-point status for paired instructions; equivalence is not established by portable tests. compare final dol text size, interpreter time, vfpu-heavy workloads, and representative gameplay with the option both off and on. keep paired singles disabled if accuracy or performance regresses.

## files

`register-paired-update.patch` records changes against the supplied archive. new differential tests are in `Wii/tests/TestVectorArithmetic.py` with the original handler retained only as a test reference. existing build flags already use size optimization for most files and dead-section elimination; those settings are retained.
