# SPDX-License-Identifier: GPL-2.0-or-later
# Host integration checks use the same Wii core, interpreter, map, and software GPU.
# They do not emulate libogc2 or Wii hardware.
include_directories(. Common ext/native Wii/tests/compat ext/native/ext/libzip ext/snappy)
add_definitions(-DHW_RVL -DPPSSPP_WII_PORTABLE_TEST -DNO_VULKAN -DMOBILE_DEVICE
  -D__STDC_CONSTANT_MACROS -DSHARED_ZLIB)
add_compile_options(-Os -ffunction-sections -fdata-sections -fno-strict-aliasing -fpermissive)
# Old headers rely on the target SDK supplying fixed-width types transitively.
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-include> $<$<COMPILE_LANGUAGE:CXX>:Wii/tests/HostConfig.h>)
include(Wii/Sources.cmake)
add_subdirectory(ext/snappy)
find_package(PNG REQUIRED)
include_directories(${PNG_INCLUDE_DIRS})
add_executable(WiiCoreIntegration ${WII_CORE_SOURCES} Wii/FileLogging.cpp Wii/Memory.cpp Wii/System.cpp
  Wii/AllocationDiagnostics.cpp Wii/Version.cpp Wii/Settings.cpp Wii/tests/CoreIntegration.cpp Wii/tests/HostMemoryUtil.cpp)
target_link_libraries(WiiCoreIntegration Ext::Snappy ${PNG_LIBRARIES} z pthread)
set_target_properties(WiiCoreIntegration PROPERTIES LINK_FLAGS "-Wl,--gc-sections")

# Optimize the interpreter and pixel loops for speed without changing IEEE
# arithmetic or guest memory ordering. Keep the remaining core size-oriented.
set_source_files_properties(
  Core/MIPS/MIPS.cpp Core/MIPS/MIPSInt.cpp Core/MIPS/MIPSIntVFPU.cpp
  Core/MIPS/MIPSTables.cpp GPU/Software/Rasterizer.cpp
  GPU/Software/Sampler.cpp GPU/Software/TransformUnit.cpp
  PROPERTIES COMPILE_FLAGS "-O3")

# Keep harness checks active in Release builds.
set_source_files_properties(Wii/tests/CoreIntegration.cpp PROPERTIES COMPILE_FLAGS "-UNDEBUG")
