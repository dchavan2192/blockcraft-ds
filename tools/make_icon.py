# Builds the 32x32 ROM icon (isometric grass block) shown in TWiLight Menu++
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from PIL import Image
import gen_assets as G

top = G.tex_grass_top()
side = G.tex_grass_side()
small = G.iso_cube(top, side, side)          # 16x16 RGBA
big = small.resize((30, 30), Image.NEAREST)
icon = Image.new("RGBA", (32, 32), (0, 0, 0, 0))
icon.paste(big, (1, 1), big)
# 16-color palette with color 0 transparent (magenta key)
bg = (255, 0, 255)
rgb = Image.new("RGB", (32, 32), bg)
rgb.paste(icon, (0, 0), icon)
pal = rgb.quantize(colors=15, method=Image.MEDIANCUT)
# rebuild so that the key color is index 0
p = pal.getpalette()[:15 * 3]
cols = [tuple(p[i * 3:i * 3 + 3]) for i in range(15)]
out = Image.new("P", (32, 32))
newpal = list(bg)
for c in cols:
    newpal += list(c)
newpal += [0] * (768 - len(newpal))
out.putpalette(newpal)
px = pal.load()
for y in range(32):
    for x in range(32):
        a = icon.getpixel((x, y))[3]
        out.putpixel((x, y), 0 if a < 128 else px[x, y] + 1)
out.info["transparency"] = 0
dst = os.path.join(os.path.dirname(__file__), "..", "gfx", "icon.gif")
out.save(dst, transparency=0)
out.convert("RGB").resize((128, 128), Image.NEAREST).save(os.path.join(os.path.dirname(__file__), "..", "screenshots", "icon.png"))
print("wrote", dst)
