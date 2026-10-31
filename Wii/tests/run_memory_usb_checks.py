#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Focused host regressions. Requires g++ and python3; does not build boot.dol."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[2]
logs=root/'Wii/validation-memory-usb'
logs.mkdir(exist_ok=True)
flags=['-pipe','-DMAX_LOGLEVEL=5','-std=c++11','-O2','-fsanitize=undefined','-fno-sanitize-recover=all','-I.','-ICommon','-Iext/native']
port=['-DHW_RVL','-DPPSSPP_WII_PORTABLE_TEST','-DNO_VULKAN','-DMOBILE_DEVICE','-include','Wii/tests/HostConfig.h']
def run(name,commands):
    output=[]
    for command in commands:
        result=subprocess.run(command,cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        output.append('$ '+' '.join(command)+'\n'+result.stdout)
        if result.returncode:
            (logs/(name+'.txt')).write_text('\n'.join(output))
            raise SystemExit(result.stdout or 'failed: '+name)
    (logs/(name+'.txt')).write_text('\n'.join(output))
    print('pass: '+name)
with tempfile.TemporaryDirectory(prefix='memory-usb-checks-',dir=Path.cwd()) as temp:
    temp=Path(temp).resolve()
    for name,sources,extra in [
        ('ge-clear-pattern',['Wii/tests/ClearPatternTests.cpp'],[]),
        ('gx-sprite-upload',['Wii/tests/GXSpriteUploadTests.cpp'],[]),
        ('gx-readback-random',['Wii/tests/GXReadbackBenchmark.cpp'],[]),
        ('gx-forced-bands',['Wii/tests/GXForcedBandTests.cpp'],[]),
        ('cached-interpreter',['Wii/tests/CachedInterpreterTests.cpp'],[]),
        ('cpu-profile',['Wii/tests/CpuProfileTests.cpp'],['-DWII_CPU_PROFILE','-DWII_PROFILE_FAKE_CLOCK','-DPPSSPP_WII_PORTABLE_TEST']),
        ('ge-row-copy',['Wii/tests/GuestRowsTests.cpp'],[]),
        ('psp1000-map',['Wii/tests/PSP1000MapTests.cpp'],[]),
        ('frame-pacer',['Wii/tests/FramePacerTests.cpp'],[]),
        ('gx-readback',['Wii/tests/GXReadbackTests.cpp'],[]),
        ('opaque-spans',['Wii/tests/OpaqueSpanTests.cpp'],[]),
        ('homebrew-path',['Wii/tests/HomebrewPathTests.cpp'],[]),
        ('lzrc-bounds',['Core/FileSystems/tlzrc.cpp','Wii/tests/LZRCBoundsTests.cpp'],port),
        ('npdrm-bounds',['Core/FileSystems/BlockDevices.cpp','Core/FileSystems/tlzrc.cpp','Wii/tests/NPDRMBoundsTests.cpp'],port+['-ffunction-sections','-fdata-sections','-Wl,--gc-sections']),
        ('display-source-memo',['Wii/tests/DisplaySourceMemoTests.cpp'],[]),
        ('lighting-prepared',['GPU/Math3D.cpp','GPU/Software/Lighting.cpp','Wii/tests/LightingPreparedTests.cpp'],port),
        ('cache-swap',['Wii/tests/CacheSwapTests.cpp'],[]),
        ('draw-vertex-memo',['Wii/tests/DrawVertexMemoTests.cpp'],[]),
        ('settings',['Wii/Settings.cpp','Wii/tests/SettingsTests.cpp'],[]),
        ('diagnostics',['Wii/Diagnostics.cpp','Wii/tests/DiagnosticsTests.cpp'],['-DPPSSPP_WII_PORTABLE_TEST','-pthread','-Wl,--wrap=fflush']),
        ('usb-loader',['Core/FileLoaders/LocalFileLoader.cpp','Wii/Settings.cpp','Wii/Diagnostics.cpp','Wii/tests/LocalLoaderUSBTests.cpp'],port+['-Wl,--wrap=read,--wrap=lseek,--wrap=memalign']),
        ('vertex-context',['Wii/tests/VertexContextTests.cpp'],port),
        ('address-space',['Wii/tests/PortTests.cpp'],port+['-fpermissive']),
        ('presentation',['Wii/tests/PresentationTests.cpp'],[]),
        ('memory-gx',['Wii/tests/MemoryGXTests.cpp'],port),
        ('storage',['Wii/Storage.cpp','Wii/tests/StorageTests.cpp'],['-IWii/tests/storage-compat']),
    ]:
        exe=temp/name
        run(name,[['g++']+flags+extra+sources+['-o',str(exe)],[str(exe)]])
    exe=temp/'allocation-diagnostics'
    run('allocation-diagnostics',[
        ['g++']+flags+['Wii/AllocationDiagnostics.cpp','Wii/tests/AllocationDiagnosticsTests.cpp','-Wl,--wrap=write','-o',str(exe)],
        [str(exe),str(temp/'allocation-failures.log')]])
    exe=temp/'allocation-link'
    run('allocation-link',[
        ['g++']+flags+['Wii/AllocationDiagnostics.cpp','Wii/tests/AllocationLinkTests.cpp','-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=memalign','-o',str(exe)],
        [str(exe),str(temp/'allocation-link.log')]])
    exe=temp/'guest-allocation-failures'
    run('guest-allocation-failures',[
        ['g++']+flags+port+['-ffunction-sections','-fdata-sections','Core/Util/BlockAllocator.cpp','Common/StringUtils.cpp','Wii/tests/GuestAllocationFailureTests.cpp','-Wl,--gc-sections','-o',str(exe)],
        [str(exe)]])
    run('compact-atlas',[['python3','Wii/tests/TestCompactAtlas.py']])
    run('vertex-memory-retry',[['python3','Wii/tests/TestVertexMemoryRetry.py']])
    run('rc8-controls',[['python3','Wii/tests/TestRC8Controls.py']])
    run('rc8-clock',[['python3','Wii/tests/TestRC8Clock.py']])
    run('rc8-overlay',[['python3','Wii/tests/TestRC8Overlay.py']])
    run('perf-syntax',[['python3','Wii/tests/TestPerfSyntax.py']])
    run('bbox-scratch',[['python3','Wii/tests/TestBoundingBoxScratch.py']])
    run('hilo-differential',[['python3','Wii/tests/TestHiLo.py']])
    run('scalar-differential',[['python3','Wii/tests/TestCPUFastPaths.py']])
    run('frame-timing',[['python3','Wii/tests/TestFrameTiming.py']])
    run('cpu-memory',[['python3','Wii/tests/TestCPUMemory.py','--asan']])
    run('memory-fast-paths',[['python3','Wii/tests/TestMemoryFastPaths.py']])
    run('opaque-texture-span',[['python3','Wii/tests/TestOpaqueTextureSpan.py']])
    run('sprite-memo',[['python3','Wii/tests/TestSpriteMemo.py']])
    run('single-core-pool',[['python3','Wii/tests/TestWiiSingleCorePool.py']])
    run('file-logging',[['python3','Wii/tests/TestFileLogging.py']])
    run('settings-menu',[['python3','Wii/tests/TestSettingsMenu.py']])
    run('source-update',[['python3','Wii/tests/TestSourceUpdate.py']])
    run('video-probe',[['python3','Wii/tests/TestVideoProbe.py']])
    run('primitive-assembly',[['python3','Wii/tests/TestPrimitiveAssembly.py']])
    run('video-preview',[['python3','Wii/tests/TestVideoPreview.py']])
    run('video-allocation',[['python3','Wii/tests/TestVideoAllocation.py']])
    run('video-shutdown',[['python3','Wii/tests/TestVideoShutdown.py']])
    run('prepared-buffer',[['python3','Wii/tests/TestPreparedBuffer.py']])
    run('ge-rasterizer',[['python3','Wii/tests/TestGERasterizer.py']])
    run('triangle-sampling',[['python3','Wii/tests/TestTriangleSampling.py']])
    run('triangle-setup',[['python3','Wii/tests/TestTriangleSetup.py']])
    run('sprite-pipeline',[['python3','Wii/tests/TestSpritePipeline.py']])
    run('dxt-texel',[['python3','Wii/tests/TestDXTTexel.py']])
    run('performance-pixels',[['python3','Wii/tests/TestPerformancePixels.py']])
    run('performance-sampler',[['python3','Wii/tests/TestPerformanceSampler.py']])
    run('performance-cso',[['python3','Wii/tests/TestPerformanceCSO.py']])
    run('clipper-fast-path',[['python3','Wii/tests/TestClipperFastPath.py']])
    run('video-modes',[['python3','Wii/tests/TestVideoModes.py']])
    run('log-summary',[['python3','Wii/tests/TestLogSummary.py']])
    run('sprite-product-syntax',[['python3','Wii/tests/TestSpriteProductSyntax.py']])
    run('interpreter-ticks',[['python3','Wii/tests/TestInterpreterTicks.py']])
    run('gx-command-mock',[['python3','Wii/tests/TestGXBridge.py'],['python3','Wii/tests/TestGXBridge.py','--full-upload']])
    run('function-hash-filter',[['python3','Wii/tests/TestFunctionHashFilter.py']])
    run('function-hash-endian',[['python3','Wii/tests/TestFunctionHashEndian.py']])
    run('arena-lifecycle',[['python3','Wii/tests/TestMemoryArena.py']])
    run('menu-aspect',[['python3','Wii/tests/TestMenuAspect.py']])
    run('ge-state-dispatch',[['python3','Wii/tests/TestGECommandDispatch.py']])
    run('ge-clear-reference',[['python3','Wii/tests/TestGEClear.py']])
    run('core-syntax',[['g++']+flags+port+['-O3','-fpermissive','-fsyntax-only',
        'Core/HLE/HLE.cpp','Core/HLE/scePower.cpp','Core/CoreTiming.cpp','Core/MemMapFunctions.cpp','Core/MIPS/MIPSTables.cpp','Core/MIPS/MIPSInt.cpp','Core/MIPS/MIPS.cpp','Core/FileLoaders/LocalFileLoader.cpp','Wii/Menu.cpp','GPU/Software/TransformUnit.cpp','GPU/Software/Lighting.cpp','GPU/Software/Clipper.cpp','GPU/Common/DrawEngineCommon.cpp','GPU/Software/Rasterizer.cpp','GPU/Software/SoftGpu.cpp','Core/HLE/sceDisplay.cpp',
        'Core/HLE/__sceAudio.cpp','Core/HLE/sceMpeg.cpp','Core/FileSystems/BlockDevices.cpp','Core/FileSystems/ISOFileSystem.cpp','Core/Util/BlockAllocator.cpp']])
print('all focused host checks passed; no target link or hardware validation')
