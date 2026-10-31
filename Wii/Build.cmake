# SPDX-License-Identifier: GPL-2.0-or-later
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Keep debug records available for runtime diagnostic_level=3.
add_definitions(-DMAX_LOGLEVEL=5)
# Experimental: requires real Broadway accuracy/performance validation.
option(WII_PAIRED_SINGLE "Experimental paired-single VFPU arithmetic" OFF)
if(WII_PAIRED_SINGLE)
  add_definitions(-DWII_PAIRED_SINGLE)
endif()
option(WII_CPU_PROFILING "Build opt-in CPU profiler" OFF)
if(WII_CPU_PROFILING)
  add_definitions(-DWII_CPU_PROFILE)
endif()
option(WII_FAST_MMU "Use the Wii software page translation table" ON)
option(WII_GX_DIRTY_UPLOAD "Only write and flush changed GX texture tiles" ON)
if(NOT WII_FAST_MMU)
  add_definitions(-DWII_REFERENCE_ADDRESS_TRANSLATION)
endif()
if(NOT WII_GX_DIRTY_UPLOAD)
  add_definitions(-DWII_GX_FULL_UPLOAD)
endif()
if(WII_PORT_HOST_TEST)
  include(Wii/tests/HostBuild.cmake)
  return()
endif()
set(CMAKE_EXECUTABLE_SUFFIX .elf)
set(DKP "$ENV{DEVKITPRO}")
set(OGC "${DKP}/libogc2/wii")
if(NOT EXISTS "${OGC}/include/ogc/libversion.h")
  message(FATAL_ERROR "Install libogc2 and libogc2-libfat (or libdvm) before building")
endif()
set(PORTLIBS "${DKP}/portlibs/ppc")

include_directories(${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/Common
  ${CMAKE_SOURCE_DIR}/Wii/compat
  ${CMAKE_SOURCE_DIR}/ext/native ${CMAKE_SOURCE_DIR}/ext/libkirk
  ${CMAKE_SOURCE_DIR}/ext/sfmt19937 ${CMAKE_SOURCE_DIR}/ext/xbrz
  ${CMAKE_SOURCE_DIR}/ext/native/ext/libzip
  ${OGC}/include ${PORTLIBS}/include)
link_directories(${OGC}/lib ${PORTLIBS}/lib)
add_definitions(-DNO_VULKAN -DMOBILE_DEVICE -D__STDC_CONSTANT_MACROS
  -DSHARED_ZLIB -D_GNU_SOURCE -DWORDS_BIGENDIAN)
add_compile_options(-Os -ffunction-sections -fdata-sections -fno-strict-aliasing)
include(Wii/Sources.cmake)
add_subdirectory(ext/snappy)
include_directories(ext/snappy)
add_executable(PPSSPPWii ${WII_CORE_SOURCES}
  Wii/Main.cpp Wii/FileLogging.cpp Wii/Settings.cpp Wii/Memory.cpp Wii/MemoryUtil.cpp Wii/System.cpp
  Wii/Video.cpp Wii/Network.cpp Wii/Version.cpp Wii/DiscBrowser.cpp Wii/Menu.cpp Wii/Storage.cpp Wii/GXRender.cpp Wii/Perf.cpp Wii/AllocationDiagnostics.cpp)
set_target_properties(PPSSPPWii PROPERTIES LINK_FLAGS
  "-mrvl -Wl,--gc-sections,-Map,PPSSPPWii.map,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=memalign,--wrap=SYS_AllocArenaMem2Lo")
target_link_libraries(PPSSPPWii Ext::Snappy png z wiiuse bte fat ogc m)
find_program(ELF2DOL elf2dol HINTS "${DKP}/tools/bin" REQUIRED)
add_custom_command(TARGET PPSSPPWii POST_BUILD
  COMMAND ${ELF2DOL} $<TARGET_FILE:PPSSPPWii> ${CMAKE_CURRENT_BINARY_DIR}/boot.dol
  COMMENT "Creating Homebrew Channel executable")

# Optimize the interpreter and pixel loops for speed without changing IEEE
# arithmetic or guest memory ordering. Keep the remaining core size-oriented.
set_source_files_properties(
  Core/MIPS/MIPS.cpp Core/MIPS/MIPSInt.cpp Core/MIPS/MIPSIntVFPU.cpp
  Core/MIPS/MIPSTables.cpp GPU/Software/Rasterizer.cpp
  GPU/Software/Sampler.cpp GPU/Software/TransformUnit.cpp
  PROPERTIES COMPILE_FLAGS "-O3")
