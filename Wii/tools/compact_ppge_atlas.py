#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compact the generated UBUNTU24 atlas without changing character lookup results."""
from pathlib import Path
import re, sys
path=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[2]/'Core/Util/ppge_atlas.cpp'
source=path.read_text()
if 'font_UBUNTU24_index[]' in source:
    print('atlas already compact');sys.exit(0)
rows=re.findall(r'^    (\{[^\n]+?\}),.*$',source,re.M)
ranges=[tuple(map(int,m)) for m in re.findall(r'^  \{ (\d+), (\d+), (\d+) \},$',source,re.M)]
assert rows and ranges and source.count('const AtlasFont font_')==1
# setdefault preserves first-match behavior for overlapping original ranges.
glyphs={}
for start,end,index in ranges:
    assert 0<=start<end<=65536 and 0<=index<=len(rows)-(end-start)
    for cp in range(start,end):glyphs.setdefault(cp,rows[index+cp-start])
kept=[(cp,row) for cp,row in sorted(glyphs.items())
      if any(float(v.strip().rstrip('f'))!=0 for v in row.strip('{}').split(',')[2:4])]
assert 0<len(kept)<=65535
lookup={cp:i+1 for i,(cp,row) in enumerate(kept)}
limit=max(lookup)+1
output='// Populated glyphs only; the index below preserves missing-character lookups.\n'
output+='const AtlasChar font_UBUNTU24_chardata[] = {\n'
output+=''.join('    '+row+',  // '+format(cp,'04x')+'\n' for cp,row in kept)+'};\n'
output+='// One-based glyph indices; zero denotes a missing glyph.\nconst unsigned short font_UBUNTU24_index[] = {\n'
for cp in range(0,limit,16):output+='  '+', '.join(str(lookup.get(i,0)) for i in range(cp,min(cp+16,limit)))+',\n'
output+='};\n'
source=source[:source.index('const AtlasChar font_')]+output+source[source.index('const AtlasFont font_'):]
source=re.sub(r'  font_UBUNTU24_ranges,\n  \d+,','  nullptr,\n  0,',source,count=1)
source=source.replace('  "UBUNTU24", // name','  "UBUNTU24", // name\n  font_UBUNTU24_index,\n  %d, // direct index covers the original unicode extent'%limit,1)
path.write_text(source)
print('compacted',len(rows),'entries to',len(kept),'glyphs and',limit,'indices')
