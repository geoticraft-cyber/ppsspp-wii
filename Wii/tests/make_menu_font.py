#!/usr/bin/env python3
# Rasterize DejaVu Sans Mono at 13px into the menu's fixed 8x16 cells.
from PIL import Image, ImageDraw, ImageFont
from pathlib import Path
font=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf',13)
rows=[]
for char in range(128):
 image=Image.new('L',(8,16));ImageDraw.Draw(image).text((0,0),chr(char) if char>=32 else ' ',font=font,fill=255)
 rows.append([sum((128>>x) for x in range(8) if image.getpixel((x,y))>=96) for y in range(16)])
Path(__file__).resolve().parent.parent.joinpath('MenuFont.h').write_text(
 '// Rasterized DejaVu Sans Mono; see FONT-LICENSE.txt.\n#pragma once\nstatic const unsigned char menuFont[128][16] = {\n'+
 ',\n'.join(' {'+','.join(hex(b) for b in row)+'}' for row in rows)+'\n};\n')
