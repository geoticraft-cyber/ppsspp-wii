#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check memo equivalence; optionally benchmark host helpers using real VertexData.
No Wii timing, executable-size or frame-rate claim follows from these samples.
"""
from pathlib import Path
import re, statistics, subprocess, sys, tempfile
root=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='draw-memo-',dir=Path.cwd()) as temp:
    temp=Path(temp)
    test=temp/'check'
    subprocess.run(['g++','-std=c++11','-O2','-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all','-I'+str(root),
                    str(root/'Wii/tests/DrawVertexMemoTests.cpp'),'-o',str(test)],check=True)
    subprocess.run([str(test)],check=True)
    if '--benchmark' not in sys.argv and '--size-only' not in sys.argv:sys.exit(0)
    flags=['g++','-std=c++11','-O3','-fno-strict-aliasing','-DHW_RVL',
           '-DPPSSPP_WII_PORTABLE_TEST','-include',str(root/'Wii/tests/HostConfig.h'),
           '-I'+str(root),'-I'+str(root/'Common'),'-I'+str(root/'ext/native')]
    # Compare the actual transform unit as well as isolated helper wrappers.
    baseline=temp/'baseline';(baseline/'Wii').mkdir(parents=True)
    reference=(root/'Wii/tests/DrawVertexMemoReference.h').read_text()
    (baseline/'Wii/DrawVertexMemo.h').write_text(reference.replace('namespace PreviousDrawMemo {','namespace Wii {',1))
    for name,extra in [('previous',['-I'+str(baseline)]),('updated',[])]:
        obj=temp/(name+'.o')
        subprocess.run(flags[:1]+extra+flags[1:]+['-fpermissive','-DNO_VULKAN','-DMOBILE_DEVICE',
                        '-c',str(root/'GPU/Software/TransformUnit.cpp'),'-o',str(obj)],check=True)
        sections=subprocess.check_output(['size','-A',str(obj)],text=True)
        text_bytes=sum(int(row.split()[1]) for row in sections.splitlines() if row.startswith('.text'))
        print('host transform unit code bytes,',name,text_bytes,flush=True)
    print(subprocess.check_output(['g++','--version'],text=True).splitlines()[0],flush=True)
    if '--benchmark' not in sys.argv:sys.exit(0)
    exe=temp/'benchmark'
    subprocess.run(flags+[str(root/'Wii/tests/DrawVertexMemoBenchmark.cpp'),'-o',str(exe)],check=True)
    symbols=subprocess.check_output(['nm','-S','-C',str(exe)],text=True)
    for name,namespace in [('previous','PreviousDrawMemo::'),('updated','Wii::')]:
        sizes={}
        for line in symbols.splitlines():
            match=re.match(r'\w+\s+(\w+)\s+[TtWw]\s+(Access<.*)',line)
            if match and namespace in match[2] and any('::'+n+'(' in match[2] for n in ['Reset','Load','Store']):
                sizes[match[2]]=int(match[1],16)
        print('host helper text bytes,',name,sum(sizes.values()),sizes,flush=True)
    samples={}
    process=subprocess.Popen([str(exe)],stdout=subprocess.PIPE,text=True)
    for line in process.stdout:
        print(line,end='',flush=True)
        row=line.strip().split(',')
        if row[0]=='mode':continue
        samples.setdefault(row[0],{'previous':[],'updated':[]})[row[2]].append(float(row[3]))
    assert process.wait()==0
    for mode,versions in samples.items():
        a,b=(statistics.median(versions[name]) for name in ['previous','updated'])
        print(f'host median7 {mode}: previous {a:.3f} ms updated {b:.3f} ms; {(a-b)/a*100:.1f}% less time',flush=True)
