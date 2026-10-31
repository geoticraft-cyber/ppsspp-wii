#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run original GE homebrew and synthetic title-compatibility fixtures.
First build the portable core with run_io_hle_checks.py. No retail game data.
"""
from pathlib import Path
import os, subprocess
from make_ge_smoke_elf import make_elf
from make_compat_fixtures import create
root=Path(__file__).resolve().parents[2]
out=root/'build-io-host';run=out/'test-run';run.mkdir(parents=True,exist_ok=True)
binary=out/'WiiCoreIntegration'
if not binary.is_file():raise SystemExit('first run Wii/tests/run_io_hle_checks.py')
def check(name,guest,ge=False,**flags):
    env=os.environ.copy()
    for key in ('WII_TEST_IO','WII_TEST_COMPAT','WII_TEST_DISPATCH','WII_WRAP_FRAME'):
        env.pop(key,None)
    env.update(flags)
    with (out/(name+'-test.log')).open('w') as log:
        subprocess.run([str(binary),str(guest),str(root/'assets')]+(['ge'] if ge else []),
                       cwd=run,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
    print('pass: '+name,flush=True)
for name,kwargs in [('ge',{}),('sprite',{'sprite':True}),
                    ('large-sprite',{'sprite':True,'large_sprite':True}),('wrapped-ge',{'wrap':True})]:
    guest=run/(name+'.elf');guest.write_bytes(make_elf(**kwargs))
    check(name,guest,True,**({'WII_WRAP_FRAME':'1'} if kwargs.get('wrap') else {}))
create(run/'compat')
for game in sorted((run/'compat').iterdir()):
    check(game.name,game,WII_TEST_COMPAT='1',WII_TEST_PSP_CLOCK='333')
