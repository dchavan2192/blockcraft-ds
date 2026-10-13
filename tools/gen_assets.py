#!/usr/bin/env python3
# BlockCraft DS asset generator.
# Produces original procedural pixel art, palettes, icons, font and sound
# effects for the game. Everything here is generated from code.
import math, os, random, struct, sys
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, "data")
SRC = os.path.join(ROOT, "source")
PREVIEW = os.path.join(ROOT, "build", "preview")
os.makedirs(DATA, exist_ok=True)
os.makedirs(PREVIEW, exist_ok=True)

T = None  # transparent marker

def clamp(v, a=0, b=255):
    return a if v < a else b if v > b else int(v)

def shade(c, f):
    return (clamp(c[0] * f), clamp(c[1] * f), clamp(c[2] * f))

def add(c, d):
    return (clamp(c[0] + d), clamp(c[1] + d), clamp(c[2] + d))

def mix(a, b, t):
    return (clamp(a[0] + (b[0] - a[0]) * t), clamp(a[1] + (b[1] - a[1]) * t), clamp(a[2] + (b[2] - a[2]) * t))

class Tex:
    def __init__(self, w=16, h=16, fill=T):
        self.w, self.h = w, h
        self.px = [[fill for _ in range(w)] for _ in range(h)]
    def get(self, x, y):
        return self.px[y % self.h][x % self.w]
    def set(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[y][x] = c
    def copy(self):
        t = Tex(self.w, self.h)
        t.px = [row[:] for row in self.px]
        return t
    def image(self):
        im = Image.new("RGBA", (self.w, self.h))
        for y in range(self.h):
            for x in range(self.w):
                c = self.px[y][x]
                im.putpixel((x, y), (0, 0, 0, 0) if c is None else (c[0], c[1], c[2], 255))
        return im

def noise_fill(t, base, var, rng, x0=0, y0=0, x1=16, y1=16):
    for y in range(y0, y1):
        for x in range(x0, x1):
            t.set(x, y, add(base, rng.randint(-var, var)))

def specks(t, colors, count, rng, x0=0, y0=0, x1=16, y1=16):
    for _ in range(count):
        t.set(rng.randrange(x0, x1), rng.randrange(y0, y1), rng.choice(colors))

def voronoi(t, n, rng, colfn, border, wrap=True):
    pts = [(rng.random() * 16, rng.random() * 16) for _ in range(n)]
    cols = [colfn(i) for i in range(n)]
    for y in range(16):
        for x in range(16):
            ds = []
            for i, (px, py) in enumerate(pts):
                dx = abs(x + 0.5 - px); dy = abs(y + 0.5 - py)
                if wrap:
                    dx = min(dx, 16 - dx); dy = min(dy, 16 - dy)
                ds.append((dx * dx + dy * dy, i))
            ds.sort()
            d0, i0 = ds[0]; d1, _ = ds[1]
            c = cols[i0]
            if math.sqrt(d1) - math.sqrt(d0) < 0.9 and border is not None:
                c = border
            t.set(x, y, add(c, rng.randint(-6, 6)))

# ----------------------------------------------------------------------------
# Block textures
# ----------------------------------------------------------------------------

TEX = []        # list of (name, Tex)
TEXIDX = {}

def reg(name, t):
    TEXIDX[name] = len(TEX)
    TEX.append((name, t))
    return t

def R(seed):
    return random.Random(seed * 7919 + 17)

STONE = (124, 124, 126)
DIRT = (125, 89, 60)
GRASS = (98, 160, 58)
SAND = (220, 208, 160)

def tex_stone(seed=1, base=STONE):
    rng = R(seed)
    t = Tex(); noise_fill(t, base, 9, rng)
    for _ in range(9):
        x, y = rng.randrange(16), rng.randrange(16)
        ln = rng.randint(2, 4)
        for i in range(ln):
            t.set(x + i, y, shade(base, 0.82))
    specks(t, [shade(base, 1.12)], 14, rng)
    return t

def tex_dirt(seed=2):
    rng = R(seed)
    t = Tex(); noise_fill(t, DIRT, 10, rng)
    specks(t, [shade(DIRT, 0.75), shade(DIRT, 1.22), (110, 110, 108)], 30, rng)
    return t

def tex_grass_top(seed=3, base=GRASS):
    rng = R(seed)
    t = Tex(); noise_fill(t, base, 14, rng)
    specks(t, [shade(base, 1.18), shade(base, 0.8)], 40, rng)
    return t

def tex_grass_side(seed=4, top=GRASS, snowy=False):
    rng = R(seed)
    t = tex_dirt(seed + 10)
    for x in range(16):
        depth = rng.choice([2, 3, 3, 4, 4, 5]) if not snowy else rng.choice([3, 3, 4, 4])
        for y in range(depth):
            c = add(top, rng.randint(-12, 12))
            if snowy:
                c = add((240, 246, 250), rng.randint(-10, 4))
            t.set(x, y, c)
        if not snowy and rng.random() < 0.35:
            t.set(x, depth, shade(top, 0.75))
    return t

def tex_sand(seed=5, base=SAND, var=7):
    rng = R(seed)
    t = Tex(); noise_fill(t, base, var, rng)
    specks(t, [shade(base, 0.9), shade(base, 1.06)], 25, rng)
    return t

def tex_gravel(seed=6):
    rng = R(seed)
    t = Tex()
    pal = [(130, 125, 122), (105, 100, 98), (150, 144, 140), (118, 106, 98), (90, 86, 86)]
    voronoi(t, 22, rng, lambda i: rng.choice(pal), (80, 76, 74))
    return t

def tex_bedrock(seed=7):
    rng = R(seed)
    t = Tex()
    for y in range(16):
        for x in range(16):
            v = rng.random()
            c = (40, 40, 40) if v < 0.35 else (85, 85, 85) if v < 0.7 else (130, 130, 130) if v < 0.9 else (20, 20, 20)
            t.set(x, y, c)
    return t

def tex_cobble(seed=8, mossy=False):
    rng = R(seed)
    t = Tex()
    voronoi(t, 9, rng, lambda i: add((122, 122, 122), rng.randint(-22, 22)), (70, 70, 72))
    # highlights on stone tops
    for y in range(16):
        for x in range(16):
            c = t.get(x, y)
            if c != (70, 70, 72) and t.get(x, y - 1)[0] < 80 and rng.random() < 0.7:
                t.set(x, y, add(c, 18))
    if mossy:
        for _ in range(6):
            cx, cy = rng.randrange(16), rng.randrange(16)
            for _ in range(10):
                x = cx + rng.randint(-2, 2); y = cy + rng.randint(-2, 2)
                t.set(x, y, add((70, 112, 52), rng.randint(-14, 14)))
    return t

def tex_log_side(seed, bark, dark, birch=False):
    rng = R(seed)
    t = Tex()
    for x in range(16):
        colc = add(bark, rng.randint(-10, 10))
        for y in range(16):
            t.set(x, y, add(colc, rng.randint(-6, 6)))
        if not birch and rng.random() < 0.4:
            for y in range(16):
                if rng.random() < 0.8:
                    t.set(x, y, add(dark, rng.randint(-6, 6)))
    if birch:
        for _ in range(8):
            x = rng.randrange(16); y = rng.randrange(16); ln = rng.randint(2, 5)
            for i in range(ln):
                t.set(x + i, y, add(dark, rng.randint(-8, 8)))
                if rng.random() < 0.4:
                    t.set(x + i, y + 1, add(dark, rng.randint(-8, 8)))
    else:
        for _ in range(10):
            x, y = rng.randrange(16), rng.randrange(16)
            t.set(x, y, shade(bark, 1.15))
    return t

def tex_log_top(seed, wood, bark):
    rng = R(seed)
    t = Tex()
    for y in range(16):
        for x in range(16):
            d = max(abs(x - 7.5), abs(y - 7.5))
            if d > 6.6:
                c = add(bark, rng.randint(-10, 10))
            else:
                ring = int(d) % 2
                c = add(shade(wood, 0.86 if ring else 1.0), rng.randint(-5, 5))
            t.set(x, y, c)
    return t

def tex_leaves(seed, base):
    rng = R(seed)
    t = Tex()
    for y in range(16):
        for x in range(16):
            v = rng.random()
            if v < 0.12:
                c = shade(base, 0.45)
            elif v < 0.4:
                c = shade(base, 0.78)
            elif v < 0.85:
                c = base
            else:
                c = shade(base, 1.2)
            t.set(x, y, add(c, rng.randint(-6, 6)))
    return t

def tex_planks(seed, wood):
    rng = R(seed)
    t = Tex()
    for board in range(4):
        y0 = board * 4
        joint = rng.randrange(3, 13)
        bc = add(wood, rng.randint(-8, 8))
        for y in range(y0, y0 + 4):
            for x in range(16):
                c = add(bc, rng.randint(-5, 5))
                if y == y0 + 3:
                    c = shade(wood, 0.62)
                elif x == joint and board % 2 == 0 or x == (joint + 8) % 16 and board % 2 == 1:
                    c = shade(wood, 0.66)
                elif rng.random() < 0.08:
                    c = shade(wood, 0.85)
                t.set(x, y, c)
    return t

def tex_ore(seed, color, hi):
    rng = R(seed)
    t = tex_stone(seed + 100)
    clusters = [(rng.randrange(2, 14), rng.randrange(2, 14)) for _ in range(4)]
    for cx, cy in clusters:
        for _ in range(5):
            x = cx + rng.randint(-1, 1); y = cy + rng.randint(-1, 1)
            t.set(x, y, color)
            if rng.random() < 0.4:
                t.set(x + 1, y, hi)
    return t

def tex_water(frame):
    rng = R(900)
    base = (44, 76, 220)
    t = Tex()
    for y in range(16):
        for x in range(16):
            v = math.sin((x + y * 0.5) * 0.7 + frame * 0.785) + math.sin((y - x * 0.3) * 0.9 - frame * 0.785 * 2)
            c = add(base, int(v * 10) + rng.randint(-3, 3))
            if v > 1.4:
                c = (110, 150, 245)
            t.set(x, y, c)
    return t

def tex_lava(frame):
    rng = R(901)
    t = Tex()
    for y in range(16):
        for x in range(16):
            v = (math.sin(x * 0.8 + frame * 0.785) + math.sin(y * 0.7 + x * 0.3 - frame * 0.785) +
                 math.sin((x + y) * 0.45 + frame * 1.57)) / 3
            if v > 0.45:
                c = (255, 230, 90)
            elif v > 0.05:
                c = (250, 160, 30)
            elif v > -0.4:
                c = (220, 100, 15)
            else:
                c = (170, 50, 10)
            t.set(x, y, add(c, rng.randint(-6, 6)))
    return t

def tex_glass():
    t = Tex()
    edge = (215, 235, 240)
    for i in range(16):
        t.set(i, 0, edge); t.set(i, 15, shade(edge, 0.85)); t.set(0, i, edge); t.set(15, i, shade(edge, 0.85))
    for i in range(4):
        t.set(3 + i, 7 - i, (240, 250, 255))
        t.set(10 + i // 2, 12 - i, (240, 250, 255))
    return t

def tex_bricks(seed=20):
    rng = R(seed)
    t = Tex()
    mortar = (170, 160, 150)
    for y in range(16):
        for x in range(16):
            row = y // 4
            off = 4 if row % 2 else 0
            if y % 4 == 3 or (x + off) % 8 == 7:
                c = add(mortar, rng.randint(-8, 8))
            else:
                c = add((150, 72, 56), rng.randint(-14, 14))
            t.set(x, y, c)
    return t

def tex_stonebricks(seed=21):
    rng = R(seed)
    t = Tex()
    for y in range(16):
        for x in range(16):
            row = y // 8
            off = 4 if row % 2 else 0
            if y % 8 == 7 or (x + off) % 8 == 7:
                c = (78, 78, 80)
            elif y % 8 == 0 or (x + off) % 8 == 0:
                c = add((150, 150, 150), rng.randint(-6, 6))
            else:
                c = add((122, 122, 124), rng.randint(-8, 8))
            t.set(x, y, c)
    return t

def tex_sandstone_side(seed=22):
    rng = R(seed)
    t = Tex()
    for y in range(16):
        band = (226, 214, 166) if y < 4 else (214, 200, 150) if y < 11 else (200, 186, 136)
        for x in range(16):
            c = add(band, rng.randint(-6, 6))
            if y in (3, 10) and rng.random() < 0.7:
                c = shade(band, 0.86)
            t.set(x, y, c)
    return t

def tex_snow(seed=23):
    rng = R(seed)
    t = Tex(); noise_fill(t, (240, 246, 250), 5, rng)
    specks(t, [(214, 226, 238)], 18, rng)
    return t

def tex_ice(seed=24):
    rng = R(seed)
    t = Tex(); noise_fill(t, (150, 190, 248), 6, rng)
    for i in range(5):
        x0 = rng.randrange(16); y0 = rng.randrange(16)
        for k in range(rng.randint(3, 7)):
            t.set(x0 + k, y0 - k // 2, (215, 235, 255))
    return t

def tex_clay(seed=25):
    rng = R(seed)
    t = Tex(); noise_fill(t, (160, 166, 180), 5, rng)
    specks(t, [(145, 150, 164), (176, 180, 192)], 30, rng)
    return t

def tex_obsidian(seed=26):
    rng = R(seed)
    t = Tex(); noise_fill(t, (22, 16, 34), 6, rng)
    for _ in range(14):
        x, y = rng.randrange(16), rng.randrange(16)
        t.set(x, y, (70, 50, 110)); t.set(x + 1, y, (48, 34, 80))
    return t

def framed_box(base_tex, frame, inner=None):
    t = base_tex.copy()
    for i in range(16):
        for c in ((i, 0), (i, 15), (0, i), (15, i)):
            t.set(c[0], c[1], frame)
    return t

def tex_crafting_top():
    t = tex_planks(31, (160, 120, 72))
    rng = R(32)
    frame = (92, 62, 34)
    for i in range(16):
        t.set(i, 0, frame); t.set(i, 15, frame); t.set(0, i, frame); t.set(15, i, frame)
    for i in range(2, 14):
        t.set(i, 5, frame); t.set(i, 10, frame); t.set(5, i, frame); t.set(10, i, frame)
    return t

def tex_crafting_side(front=False):
    t = tex_planks(33, (150, 110, 64))
    dark = (70, 46, 24)
    for i in range(16):
        t.set(i, 0, (92, 62, 34)); t.set(i, 1, (120, 84, 46))
    if front:
        # saw
        for x in range(3, 10):
            t.set(x, 6, (190, 190, 196)); t.set(x, 7, (160, 160, 168))
            if x % 2:
                t.set(x, 8, (140, 140, 146))
        for y in range(5, 9):
            t.set(10, y, dark); t.set(11, y, dark)
    else:
        # hammer
        for x in range(4, 10):
            t.set(x, 5, (120, 120, 126)); t.set(x, 6, (100, 100, 106))
        for y in range(7, 13):
            t.set(6, y, dark); t.set(7, y, (96, 66, 36))
    return t

def tex_furnace(kind):
    rng = R(40)
    t = tex_stone(41, (118, 118, 120))
    for i in range(16):
        t.set(i, 0, (90, 90, 92)); t.set(i, 15, (90, 90, 92)); t.set(0, i, (90, 90, 92)); t.set(15, i, (90, 90, 92))
    if kind == "top":
        for i in range(2, 14):
            t.set(i, 2, (100, 100, 102)); t.set(i, 13, (100, 100, 102))
        return t
    if kind == "side":
        return t
    # front
    for y in range(8, 13):
        for x in range(4, 12):
            t.set(x, y, (30, 30, 32))
    for x in range(4, 12):
        t.set(x, 7, (70, 70, 72)); t.set(x, 13, (70, 70, 72))
    for x in range(5, 11, 2):
        t.set(x, 4, (60, 60, 62))
    if kind == "front_lit":
        for x in range(5, 11):
            h = rng.randint(1, 4)
            for y in range(12 - h, 13):
                t.set(x, y, (255, 200, 60) if y > 12 - h else (255, 110, 30))
    return t

def tex_chest(kind):
    rng = R(50)
    wood = (164, 112, 52)
    t = Tex()
    noise_fill(t, wood, 8, rng)
    for x in range(16):
        for y in (0, 15):
            t.set(x, y, (88, 56, 22))
        t.set(0, x, (88, 56, 22)); t.set(15, x, (88, 56, 22))
    if kind in ("front", "side"):
        for x in range(16):
            t.set(x, 5, (88, 56, 22))
        if kind == "front":
            for y in range(4, 8):
                for x in range(7, 9):
                    t.set(x, y, (200, 200, 205))
            t.set(7, 7, (120, 120, 126)); t.set(8, 7, (120, 120, 126))
    return t

def tex_torch():
    t = Tex()
    for y in range(6, 16):
        t.set(7, y, (110, 80, 44)); t.set(8, y, (88, 62, 32))
    t.set(7, 5, (255, 220, 120)); t.set(8, 5, (255, 200, 80))
    t.set(7, 4, (255, 250, 200)); t.set(8, 4, (255, 230, 140))
    t.set(7, 3, (255, 170, 60)); t.set(8, 6, (255, 150, 40))
    return t

def tex_flower(petal, center, seed):
    rng = R(seed)
    t = Tex()
    stem = (56, 120, 40)
    for y in range(8, 16):
        t.set(8, y, stem)
    t.set(6, 12, stem); t.set(7, 11, stem); t.set(9, 13, stem); t.set(10, 12, stem)
    for dx, dy in [(0, -1), (1, 0), (-1, 0), (0, 1), (1, -1), (-1, -1), (1, 1), (-1, 1)]:
        t.set(8 + dx, 6 + dy, petal)
    t.set(8, 4, petal); t.set(10, 6, petal); t.set(6, 6, petal)
    t.set(8, 6, center)
    return t

def tex_tallgrass(seed=60):
    rng = R(seed)
    t = Tex()
    for i in range(9):
        x = rng.randrange(1, 15); h = rng.randint(5, 12)
        c = add((88, 150, 50), rng.randint(-20, 15))
        for y in range(16 - h, 16):
            xx = x + (1 if (y < 16 - h + 2 and i % 2) else 0)
            t.set(xx, y, c)
    return t

def tex_deadbush(seed=61):
    rng = R(seed)
    t = Tex()
    c = (122, 88, 46)
    for y in range(9, 16):
        t.set(8, y, c)
    for (x, y) in [(7, 9), (6, 8), (5, 7), (9, 10), (10, 9), (11, 8), (12, 7), (6, 11), (5, 10), (9, 7), (9, 6), (10, 5)]:
        t.set(x, y, c)
    return t

def tex_sapling(leaf, trunk, seed):
    rng = R(seed)
    t = Tex()
    for y in range(10, 16):
        t.set(8, y, trunk)
    for y in range(3, 11):
        w = 3 - abs(y - 7) // 2
        for x in range(8 - w, 9 + w):
            if rng.random() < 0.85:
                t.set(x, y, add(leaf, rng.randint(-15, 15)))
    return t

def tex_mushroom(cap, dots, seed):
    t = Tex()
    stem = (220, 210, 190)
    for y in range(10, 15):
        t.set(7, y, stem); t.set(8, y, stem)
    for y in range(6, 10):
        w = 2 + (y - 6)
        for x in range(8 - w, 8 + w):
            t.set(x, y, cap)
    if dots:
        for (x, y) in [(6, 7), (9, 8), (5, 9), (10, 9)]:
            t.set(x, y, dots)
    return t

def tex_cactus(kind, seed=70):
    rng = R(seed)
    t = Tex()
    base = (60, 140, 50)
    for y in range(16):
        for x in range(16):
            if kind == "side" and (x < 1 or x > 14):
                continue
            c = add(base, rng.randint(-8, 8))
            if kind == "side" and x in (3, 7, 11):
                c = shade(base, 0.75)
            t.set(x, y, c)
    if kind == "side":
        for _ in range(10):
            t.set(rng.choice([2, 6, 10, 13]), rng.randrange(16), (230, 230, 200))
    else:
        for i in range(16):
            t.set(i, 0, shade(base, 0.7)); t.set(i, 15, shade(base, 0.7)); t.set(0, i, shade(base, 0.7)); t.set(15, i, shade(base, 0.7))
        t.set(7, 7, (150, 200, 100)); t.set(8, 8, (150, 200, 100))
    return t

WOOL_COLORS = [
    ("white", (234, 236, 236)), ("orange", (240, 118, 20)), ("magenta", (190, 70, 180)),
    ("light_blue", (60, 176, 218)), ("yellow", (248, 198, 40)), ("lime", (112, 186, 26)),
    ("pink", (238, 142, 172)), ("gray", (62, 68, 72)), ("light_gray", (142, 142, 136)),
    ("cyan", (22, 138, 145)), ("purple", (122, 42, 172)), ("blue", (52, 56, 158)),
    ("brown", (114, 72, 40)), ("green", (84, 110, 28)), ("red", (160, 38, 34)), ("black", (22, 22, 26)),
]

def tex_wool(color, seed):
    rng = R(seed)
    t = Tex()
    for y in range(16):
        for x in range(16):
            v = math.sin(x * 1.3 + y * 0.6) * 0.5 + math.sin(y * 1.7 - x * 0.4) * 0.5
            t.set(x, y, shade(color, 0.92 + v * 0.06 + rng.uniform(-0.04, 0.04)))
    return t

def tex_tnt(kind):
    rng = R(80)
    t = Tex()
    if kind == "side":
        for y in range(16):
            for x in range(16):
                c = add((200, 50, 40), rng.randint(-10, 10))
                if 5 <= y <= 10:
                    c = (230, 230, 225)
                if x % 4 == 3 and not (5 <= y <= 10):
                    c = (150, 34, 28)
                t.set(x, y, c)
        # simple letters "TNT" in 3x4 cells
        T_ = ["###", ".#.", ".#.", ".#."]
        N_ = ["#.#", "###", "###", "#.#"]
        for li, L in enumerate([T_, N_, T_]):
            for yy in range(4):
                for xx in range(3):
                    if L[yy][xx] == "#":
                        t.set(2 + li * 4 + xx, 6 + yy, (30, 30, 30))
    elif kind == "top":
        noise_fill(t, (200, 60, 50), 8, rng)
        for y in range(5, 11):
            for x in range(5, 11):
                t.set(x, y, (140, 140, 140))
        t.set(7, 7, (40, 40, 40)); t.set(8, 8, (40, 40, 40)); t.set(7, 8, (60, 60, 60)); t.set(8, 7, (60, 60, 60))
    else:
        noise_fill(t, (190, 55, 45), 8, rng)
    return t

def tex_bookshelf():
    rng = R(90)
    t = tex_planks(91, (160, 120, 72))
    spines = [(150, 40, 40), (40, 70, 150), (60, 120, 50), (180, 150, 60), (110, 60, 120), (140, 90, 50)]
    for shelf in (1, 9):
        x = 1
        while x < 15:
            w = rng.choice([1, 2])
            c = rng.choice(spines)
            h = rng.randint(4, 6)
            for xx in range(x, min(15, x + w)):
                for y in range(shelf + 6 - h, shelf + 6):
                    t.set(xx, y, add(c, rng.randint(-8, 8)))
            x += w
    return t

def tex_metal_block(base, seed):
    rng = R(seed)
    t = Tex()
    for y in range(16):
        for x in range(16):
            c = add(base, rng.randint(-5, 5))
            if x == 0 or y == 0:
                c = shade(base, 1.18)
            if x == 15 or y == 15:
                c = shade(base, 0.7)
            if (x + y) % 9 == 0 and 2 < x < 13:
                c = shade(base, 1.12)
            t.set(x, y, c)
    return t

def tex_farmland(wet):
    rng = R(100 + wet)
    base = (86, 58, 36) if wet else (120, 86, 58)
    t = Tex()
    for y in range(16):
        for x in range(16):
            c = add(base, rng.randint(-8, 8))
            if y % 4 == 0:
                c = shade(base, 0.7)
            t.set(x, y, c)
    return t

def tex_wheat(stage):
    rng = R(110 + stage)
    t = Tex()
    h = 3 + stage * 12 // 7
    for i, x in enumerate([2, 5, 8, 11, 13]):
        ph = h - (i % 2)
        for y in range(16 - ph, 16):
            g = mix((70, 150, 40), (200, 170, 70), stage / 7.0)
            t.set(x, y, add(g, rng.randint(-10, 10)))
        if stage >= 5:
            for y in range(16 - ph - 1, 16 - ph + 3):
                t.set(x + 1, y, (190, 160, 60))
    return t

def tex_ladder():
    t = Tex()
    wood = (130, 96, 54)
    for y in range(16):
        t.set(2, y, wood); t.set(3, y, shade(wood, 0.8)); t.set(12, y, wood); t.set(13, y, shade(wood, 0.8))
    for y in (2, 6, 10, 14):
        for x in range(4, 12):
            t.set(x, y, shade(wood, 1.1))
    return t

def tex_pumpkin(kind, lit=False):
    rng = R(120)
    base = (220, 128, 24)
    t = Tex()
    for y in range(16):
        for x in range(16):
            c = add(base, rng.randint(-8, 8))
            if kind != "top" and x % 4 == 0:
                c = shade(base, 0.8)
            t.set(x, y, c)
    if kind == "top":
        for y in range(16):
            for x in range(16):
                d = abs(x - 7.5) + abs(y - 7.5)
                if int(d) % 4 == 0:
                    t.set(x, y, shade(base, 0.82))
        for (x, y) in [(7, 7), (8, 7), (7, 8), (8, 8)]:
            t.set(x, y, (90, 110, 40))
    if kind == "face":
        hole = (255, 220, 80) if lit else (40, 26, 8)
        for (x, y) in [(4, 5), (5, 5), (4, 6), (5, 6), (10, 5), (11, 5), (10, 6), (11, 6)]:
            t.set(x, y, hole)
        for x in range(3, 13):
            t.set(x, 10, hole)
        for x in (4, 6, 8, 10):
            t.set(x, 11, hole)
        for x in (5, 7, 9, 11):
            t.set(x, 9, hole)
    return t

def tex_bed(kind):
    rng = R(130)
    t = Tex()
    red = (176, 36, 36)
    wood = (140, 100, 56)
    if kind == "head_top":
        for y in range(16):
            for x in range(16):
                c = add((238, 238, 238), rng.randint(-6, 4)) if y < 7 else add(red, rng.randint(-8, 8))
                t.set(x, y, c)
    elif kind == "foot_top":
        for y in range(16):
            for x in range(16):
                t.set(x, y, add(red, rng.randint(-8, 8)))
        for x in range(16):
            t.set(x, 14, shade(red, 0.75))
    else:  # side
        for y in range(16):
            for x in range(16):
                if y < 7:
                    t.set(x, y, T)
                elif y < 11:
                    t.set(x, y, add(red, rng.randint(-8, 8)))
                else:
                    t.set(x, y, add(wood, rng.randint(-8, 8)) if (x < 3 or y < 13) else T)
    return t

def tex_crack(stage):
    rng = R(140)
    t = Tex()
    pts = [(rng.randrange(16), rng.randrange(16)) for _ in range(20)]
    n = 2 + stage * 2
    for i in range(n):
        x, y = pts[i % len(pts)]
        dx, dy = rng.choice([(1, 0), (0, 1), (1, 1), (-1, 1)])
        ln = 2 + stage // 2
        for k in range(ln):
            t.set(x + dx * k, y + dy * k, (30, 30, 30))
    return t

def tex_sun():
    t = Tex()
    for y in range(16):
        for x in range(16):
            d = max(abs(x - 7.5), abs(y - 7.5))
            if d < 5:
                t.set(x, y, (255, 252, 215))
            elif d < 7:
                t.set(x, y, (255, 230, 120))
    return t

def tex_moon():
    rng = R(150)
    t = Tex()
    for y in range(16):
        for x in range(16):
            d = max(abs(x - 7.5), abs(y - 7.5))
            if d < 6:
                t.set(x, y, add((210, 214, 226), rng.randint(-8, 8)))
    for (x, y) in [(5, 5), (6, 5), (9, 8), (10, 9), (6, 10)]:
        t.set(x, y, (160, 164, 180))
    return t

def tex_door(kind, wood):
    rng = R(160)
    t = tex_planks(161, wood)
    for y in range(16):
        t.set(0, y, shade(wood, 0.6)); t.set(15, y, shade(wood, 0.6))
    if kind == "top":
        for y in range(3, 9):
            for x in range(3, 13):
                if x in (3, 12) or y in (3, 8) or x == 7 or x == 8:
                    t.set(x, y, shade(wood, 0.6))
                else:
                    t.set(x, y, T)
    else:
        t.set(12, 5, (200, 200, 205)); t.set(12, 6, (200, 200, 205))
    return t

def tex_fire(frame):
    rng = R(170 + frame)
    t = Tex()
    for x in range(16):
        h = rng.randint(6, 15)
        for y in range(16 - h, 16):
            f = (y - (16 - h)) / max(1, h)
            c = (255, 240, 120) if f > 0.7 else (255, 170, 40) if f > 0.35 else (220, 80, 20)
            t.set(x, y, c)
    return t

# Mob skins -----------------------------------------------------------------

def tex_face(base, eyes, mouth=None, seed=0, eyecol2=None, extra=None):
    rng = R(seed)
    t = Tex(); noise_fill(t, base, 6, rng)
    for (x, y, c) in eyes:
        t.set(x, y, c)
    if mouth:
        for (x, y, c) in mouth:
            t.set(x, y, c)
    if extra:
        extra(t, rng)
    return t

def tex_skin(base, seed, var=6, spots=None):
    rng = R(seed)
    t = Tex(); noise_fill(t, base, var, rng)
    if spots:
        col, n = spots
        for _ in range(n):
            cx, cy = rng.randrange(16), rng.randrange(16)
            r = rng.randint(2, 4)
            for y in range(cy - r, cy + r):
                for x in range(cx - r, cx + r):
                    if (x - cx) ** 2 + (y - cy) ** 2 < r * r:
                        t.set(x % 16, y % 16, add(col, rng.randint(-6, 6)))
    return t

def eye_pair(xl, xr, y, white, pupil):
    return [(xl, y, white), (xl + 1, y, pupil), (xr, y, pupil), (xr + 1, y, white)]

def build_block_textures():
    reg("grass_top", tex_grass_top())
    reg("grass_side", tex_grass_side())
    reg("dirt", tex_dirt())
    reg("stone", tex_stone())
    reg("cobblestone", tex_cobble())
    reg("bedrock", tex_bedrock())
    reg("sand", tex_sand())
    reg("gravel", tex_gravel())
    reg("log_oak", tex_log_side(10, (104, 82, 50), (74, 58, 34)))
    reg("log_oak_top", tex_log_top(11, (178, 140, 86), (104, 82, 50)))
    reg("leaves_oak", tex_leaves(12, (66, 136, 40)))
    reg("planks_oak", tex_planks(13, (164, 128, 78)))
    reg("log_birch", tex_log_side(14, (220, 220, 210), (50, 50, 46), birch=True))
    reg("log_birch_top", tex_log_top(15, (200, 180, 130), (220, 220, 210)))
    reg("leaves_birch", tex_leaves(16, (100, 150, 60)))
    reg("planks_birch", tex_planks(17, (196, 176, 120)))
    reg("log_spruce", tex_log_side(18, (70, 50, 30), (50, 34, 20)))
    reg("log_spruce_top", tex_log_top(19, (120, 88, 56), (70, 50, 30)))
    reg("leaves_spruce", tex_leaves(20, (46, 92, 56)))
    reg("planks_spruce", tex_planks(21, (110, 80, 50)))
    reg("coal_ore", tex_ore(22, (34, 34, 34), (60, 60, 60)))
    reg("iron_ore", tex_ore(23, (210, 170, 140), (230, 200, 176)))
    reg("gold_ore", tex_ore(24, (250, 210, 50), (255, 245, 150)))
    reg("diamond_ore", tex_ore(25, (90, 220, 220), (190, 255, 250)))
    reg("redstone_ore", tex_ore(26, (190, 20, 20), (255, 60, 60)))
    reg("water", tex_water(0))
    reg("lava", tex_lava(0))
    reg("glass", tex_glass())
    reg("bricks", tex_bricks())
    reg("sandstone", tex_sandstone_side())
    reg("sandstone_top", tex_sand(27, (226, 214, 166), 4))
    reg("snow", tex_snow())
    reg("grass_side_snow", tex_grass_side(28, snowy=True))
    reg("ice", tex_ice())
    reg("clay", tex_clay())
    reg("obsidian", tex_obsidian())
    reg("crafting_top", tex_crafting_top())
    reg("crafting_side", tex_crafting_side(False))
    reg("crafting_front", tex_crafting_side(True))
    reg("furnace_front", tex_furnace("front"))
    reg("furnace_front_lit", tex_furnace("front_lit"))
    reg("furnace_side", tex_furnace("side"))
    reg("furnace_top", tex_furnace("top"))
    reg("chest_front", tex_chest("front"))
    reg("chest_side", tex_chest("side"))
    reg("chest_top", tex_chest("top"))
    reg("torch", tex_torch())
    reg("flower_red", tex_flower((210, 30, 30), (40, 20, 10), 1))
    reg("flower_yellow", tex_flower((250, 220, 40), (230, 160, 20), 2))
    reg("tallgrass", tex_tallgrass())
    reg("deadbush", tex_deadbush())
    reg("sapling_oak", tex_sapling((66, 136, 40), (104, 82, 50), 3))
    reg("sapling_birch", tex_sapling((110, 160, 70), (220, 220, 210), 4))
    reg("sapling_spruce", tex_sapling((46, 92, 56), (70, 50, 30), 5))
    reg("mushroom_red", tex_mushroom((200, 30, 30), (240, 240, 240), 6))
    reg("mushroom_brown", tex_mushroom((150, 110, 80), None, 7))
    reg("cactus_side", tex_cactus("side"))
    reg("cactus_top", tex_cactus("top"))
    for i, (nm, col) in enumerate(WOOL_COLORS):
        reg("wool_" + nm, tex_wool(col, 200 + i))
    reg("tnt_side", tex_tnt("side"))
    reg("tnt_top", tex_tnt("top"))
    reg("tnt_bottom", tex_tnt("bottom"))
    reg("bookshelf", tex_bookshelf())
    reg("mossy_cobblestone", tex_cobble(9, mossy=True))
    reg("stonebricks", tex_stonebricks())
    reg("iron_block", tex_metal_block((210, 210, 214), 300))
    reg("gold_block", tex_metal_block((248, 210, 60), 301))
    reg("diamond_block", tex_metal_block((110, 230, 224), 302))
    reg("coal_block", tex_metal_block((30, 30, 32), 303))
    reg("farmland_dry", tex_farmland(0))
    reg("farmland_wet", tex_farmland(1))
    for s in range(8):
        reg("wheat_%d" % s, tex_wheat(s))
    reg("ladder", tex_ladder())
    reg("pumpkin_side", tex_pumpkin("side"))
    reg("pumpkin_top", tex_pumpkin("top"))
    reg("pumpkin_face", tex_pumpkin("face"))
    reg("jackolantern_face", tex_pumpkin("face", lit=True))
    reg("bed_head_top", tex_bed("head_top"))
    reg("bed_foot_top", tex_bed("foot_top"))
    reg("bed_side", tex_bed("side"))
    reg("door_top", tex_door("top", (156, 118, 70)))
    reg("door_bottom", tex_door("bottom", (156, 118, 70)))
    reg("fire", tex_fire(0))
    for s in range(10):
        reg("crack_%d" % s, tex_crack(s))
    reg("sun", tex_sun())
    reg("moon", tex_moon())
    # mobs
    PINK = (236, 160, 160)
    reg("pig_face", tex_face(PINK, eye_pair(3, 11, 5, (250, 250, 250), (20, 20, 20)), seed=500,
                              extra=lambda t, r: [t.set(x, y, (200, 110, 120)) for x in range(5, 11) for y in range(8, 12)] and [t.set(x, 10, (120, 60, 70)) for x in (6, 9)]))
    reg("pig_skin", tex_skin(PINK, 501, 8))
    reg("cow_face", tex_face((70, 52, 40), eye_pair(3, 11, 5, (240, 240, 240), (10, 10, 10)), seed=502,
                              extra=lambda t, r: [t.set(x, y, (220, 210, 200)) for x in range(4, 12) for y in range(9, 14)] and [t.set(x, 11, (90, 60, 60)) for x in (6, 9)]))
    reg("cow_skin", tex_skin((70, 52, 40), 503, 6, spots=((225, 222, 215), 3)))
    reg("sheep_face", tex_face((210, 190, 170), eye_pair(3, 11, 6, (240, 240, 240), (10, 10, 10)), seed=504,
                                extra=lambda t, r: [t.set(x, 10, (200, 150, 150)) for x in range(6, 10)]))
    reg("sheep_wool", tex_wool((232, 232, 228), 505))
    reg("sheep_skin", tex_skin((210, 190, 170), 506, 5))
    reg("chicken_face", tex_face((245, 245, 240), eye_pair(3, 11, 4, (245, 245, 240), (10, 10, 10)), seed=507,
                                  extra=lambda t, r: [t.set(x, y, (250, 180, 30)) for x in range(5, 11) for y in range(7, 10)] and [t.set(x, y, (210, 30, 30)) for x in range(6, 10) for y in range(10, 13)]))
    reg("chicken_feathers", tex_skin((245, 245, 240), 508, 6))
    ZG = (90, 150, 80)
    reg("zombie_face", tex_face(ZG, [(3, 6, (20, 30, 20)), (4, 6, (20, 30, 20)), (11, 6, (20, 30, 20)), (12, 6, (20, 30, 20))],
                                 [(x, 11, (50, 80, 45)) for x in range(5, 11)], seed=509,
                                 extra=lambda t, r: [t.set(x, y, (40, 70, 36)) for x in range(16) for y in range(0, 2)]))
    reg("zombie_skin", tex_skin(ZG, 510, 7))
    reg("zombie_shirt", tex_skin((40, 160, 160), 511, 8))
    reg("zombie_pants", tex_skin((60, 50, 140), 512, 8))
    BONE = (200, 200, 196)
    reg("skeleton_face", tex_face(BONE, [(x, y, (30, 30, 30)) for x in (3, 4, 11, 12) for y in (5, 6)],
                                   [(x, 11, (60, 60, 60)) for x in range(4, 12, 2)] + [(7, 8, (50, 50, 50)), (8, 8, (50, 50, 50))], seed=513))
    reg("skeleton_bone", tex_skin(BONE, 514, 10))
    reg("spider_face", tex_face((40, 36, 34), [(x, y, (230, 20, 20)) for (x, y) in [(3, 5), (5, 4), (10, 4), (12, 5), (6, 7), (9, 7), (4, 7), (11, 7)]], seed=515))
    reg("spider_body", tex_skin((44, 38, 34), 516, 10))
    CAP = (200, 40, 40)
    reg("shroom_cap", tex_skin(CAP, 517, 6, spots=((240, 240, 230), 4)))
    reg("shroom_stem", tex_skin((228, 218, 196), 518, 6))
    reg("shroom_face", tex_face((228, 218, 196), [(x, y, (20, 20, 20)) for x in (3, 4, 11, 12) for y in (5, 6)],
                                 [(x, y, (20, 20, 20)) for x in range(6, 10) for y in (10, 11)] + [(5, 12, (20, 20, 20)), (10, 12, (20, 20, 20))], seed=519))
    reg("player_skin", tex_skin((200, 150, 110), 520, 5))
    reg("player_sleeve", tex_skin((40, 120, 170), 521, 6))
    reg("white", Tex(fill=(255, 255, 255)))

# Animated frames are stored after the main table.
ANIM_FRAMES = 8

# ----------------------------------------------------------------------------
# Item icons (16x16 RGBA) for UI + 3D drops
# ----------------------------------------------------------------------------

ICONS = []      # (name, PIL RGBA 16x16)
ICONIDX = {}

def reg_icon(name, im):
    ICONIDX[name] = len(ICONS)
    ICONS.append((name, im))

def outline(im, col=(30, 26, 22, 255)):
    out = im.copy()
    px = im.load(); po = out.load()
    for y in range(16):
        for x in range(16):
            if px[x, y][3] == 0:
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    xx, yy = x + dx, y + dy
                    if 0 <= xx < 16 and 0 <= yy < 16 and px[xx, yy][3] > 0 and px[xx, yy][:3] != col[:3]:
                        po[x, y] = col
                        break
    return out

def iso_cube(top, left, right):
    """Render an isometric cube icon from three 16x16 textures."""
    im = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
    W = 16
    for py in range(16):
        for px in range(16):
            # inverse map: cube corners
            X = px + 0.5; Y = py + 0.5
            cx = 8.0
            # top face: diamond centered at (8, 4), half-width 7, half-height 3.5
            u = (X - cx) / 7.0; v = (Y - 4.0) / 3.6
            a = (v + u); b = (v - u)
            if -1 <= a <= 1 and -1 <= b <= 1:
                tx = int((a + 1) / 2 * 15.99); ty = int((b + 1) / 2 * 15.99)
                c = top.get(15 - ty, tx) if False else top.get(tx, 15 - ty)
                if c is not None:
                    im.putpixel((px, py), (*c, 255))
                continue
            # left face
            if X <= cx and X >= 1:
                s = (X - 1) / 7.0
                ytop = 4.0 + (s) * 3.6
                yrel = (Y - ytop) / 8.4
                if 0 <= yrel <= 1 and Y >= 4.0 + 0 * s:
                    c = left.get(int(s * 15.99), int(yrel * 15.99))
                    if c is not None:
                        im.putpixel((px, py), (*shade(c, 0.78), 255))
                    continue
            if X > cx and X <= 15:
                s = (X - cx) / 7.0
                ytop = 4.0 + (1 - s) * 3.6
                yrel = (Y - ytop) / 8.4
                if 0 <= yrel <= 1:
                    c = right.get(int(s * 15.99), int(yrel * 15.99))
                    if c is not None:
                        im.putpixel((px, py), (*shade(c, 0.6), 255))
    return im

def flat_icon(tex):
    return tex.image()

MAT = {
    "wood": ((150, 112, 62), (110, 80, 42)),
    "stone": ((140, 140, 140), (96, 96, 96)),
    "iron": ((226, 226, 226), (170, 170, 176)),
    "gold": ((250, 222, 70), (210, 160, 30)),
    "diamond": ((120, 240, 230), (40, 180, 170)),
}
STICK = ((140, 100, 50), (96, 66, 32))

def draw_px(im, pts, col):
    for (x, y) in pts:
        if 0 <= x < 16 and 0 <= y < 16:
            im.putpixel((x, y), (*col, 255))

def handle(im, n=9, x0=3, y0=13):
    for i in range(n):
        draw_px(im, [(x0 + i, y0 - i)], STICK[0])
        draw_px(im, [(x0 + i + 1, y0 - i)], STICK[1])

def icon_tool(kind, mat):
    hi, lo = MAT[mat]
    im = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
    if kind == "sword":
        for i in range(10):
            draw_px(im, [(5 + i, 10 - i)], hi)
            draw_px(im, [(5 + i, 11 - i)], lo)
            draw_px(im, [(6 + i, 10 - i)], lo if i % 3 else hi)
        draw_px(im, [(2, 13), (3, 12), (4, 11)], STICK[0])
        draw_px(im, [(3, 13), (4, 12)], STICK[1])
        draw_px(im, [(3, 9), (4, 10), (6, 12), (7, 13), (5, 11)], (90, 70, 40))
        draw_px(im, [(1, 14)], lo)
    else:
        handle(im, 9 if kind != "shovel" else 8)
        if kind == "pickaxe":
            arc = [(5, 2), (6, 2), (7, 2), (8, 2), (9, 2), (10, 3), (11, 3), (12, 4), (13, 5), (13, 6), (14, 7), (14, 8), (14, 9)]
            arc2 = [(5, 3), (6, 3), (7, 3), (8, 3), (9, 3), (10, 4), (11, 4), (12, 5), (12, 6), (13, 7), (13, 8), (13, 9)]
            draw_px(im, arc, hi); draw_px(im, arc2, lo)
        elif kind == "axe":
            blade = [(8, 2), (9, 2), (10, 2), (8, 3), (9, 3), (10, 3), (11, 3), (8, 4), (9, 4), (10, 4), (11, 4), (12, 4),
                     (9, 5), (10, 5), (11, 5), (12, 5), (13, 5), (11, 6), (12, 6), (13, 6)]
            draw_px(im, blade, hi)
            draw_px(im, [(8, 4), (9, 5), (11, 6), (12, 6)], lo)
        elif kind == "shovel":
            head = [(10, 3), (11, 3), (12, 3), (13, 4), (9, 4), (10, 4), (11, 4), (12, 4), (10, 5), (11, 5), (12, 5), (13, 5),
                    (11, 6), (12, 6), (13, 2), (12, 2), (14, 3), (14, 4)]
            draw_px(im, head, hi)
            draw_px(im, [(10, 5), (11, 6), (12, 6)], lo)
        elif kind == "hoe":
            draw_px(im, [(8, 3), (9, 3), (10, 3), (11, 4), (12, 4)], hi)
            draw_px(im, [(8, 4), (9, 4), (13, 4)], lo)
            draw_px(im, [(7, 3), (7, 4)], hi)
    return outline(im)

def icon_from_mask(mask, colors):
    im = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
    for y, row in enumerate(mask):
        for x, ch in enumerate(row):
            if ch in colors:
                im.putpixel((x, y), (*colors[ch], 255))
    return outline(im)

M_INGOT = [
    "................",
    "................",
    "................",
    "................",
    "................",
    "......hhhhhh....",
    ".....hHHHHHHh...",
    "....hHHHHHHHHh..",
    "...hHHHHHHHHHHl.",
    "..hHHHHHHHHHHll.",
    "..llllllllllll..",
    "................",
    "................",
    "................",
    "................",
    "................",
]
M_GEM = [
    "................",
    "................",
    "................",
    ".....hhhhhh.....",
    "....hHHHHHHh....",
    "...hHhHHHHHHl...",
    "..hHHHHHHHHHHl..",
    "..lHHHHHHHHHHl..",
    "...lHHHHHHHHl...",
    "....lHHHHHHl....",
    ".....lHHHHl.....",
    "......lHHl......",
    ".......ll.......",
    "................",
    "................",
    "................",
]
M_LUMP = [
    "................",
    "................",
    "................",
    "................",
    "......hhh.......",
    ".....hHHHhh.....",
    "....hHHlHHHh....",
    "...hHHHHHHlHl...",
    "...HHlHHHHHHl...",
    "...lHHHHlHHHl...",
    "....lHHHHHHl....",
    ".....llHHll.....",
    ".......ll.......",
    "................",
    "................",
    "................",
]
M_STICK = [
    "................",
    "................",
    ".............h..",
    "............hl..",
    "...........hl...",
    "..........hl....",
    ".........hl.....",
    "........hl......",
    ".......hl.......",
    "......hl........",
    ".....hl.........",
    "....hl..........",
    "...hl...........",
    "..hl............",
    "................",
    "................",
]
M_MEAT = [
    "................",
    "................",
    "................",
    "......HHHH......",
    "....HHHHHHHH....",
    "...HHhHHHHHHH...",
    "..HHHHHHHhHHHH..",
    "..HHHHHHHHHHHH..",
    "..HhHHHHHHHHHl..",
    "...HHHHHHHHHl...",
    "....lHHHHHll.ww.",
    ".....llll..wWw..",
    "...........ww...",
    "................",
    "................",
    "................",
]
M_BREAD = [
    "................",
    "................",
    "................",
    "................",
    "......hhhhh.....",
    "....hhHHHHHhh...",
    "...hHHlHHlHHHh..",
    "..hHHHHHHHHHHHh.",
    "..HHlHHlHHlHHHl.",
    "..lHHHHHHHHHHll.",
    "...llllllllll...",
    "................",
    "................",
    "................",
    "................",
    "................",
]
M_APPLE = [
    "................",
    "........s.......",
    ".......s.gg.....",
    ".......sgg......",
    "....HHH.HHH.....",
    "...HHHHHHHHH....",
    "..HhhHHHHHHHH...",
    "..HhHHHHHHHHH...",
    "..HHHHHHHHHHl...",
    "..HHHHHHHHHHl...",
    "...HHHHHHHHl....",
    "....HHlHHll.....",
    ".....ll.ll......",
    "................",
    "................",
    "................",
]
M_BUCKET = [
    "................",
    "................",
    "...hhhhhhhhhh...",
    "..h..........h..",
    "..hFFFFFFFFFFh..",
    "..hFFFFFFFFFFh..",
    "...HFFFFFFFFl...",
    "...HHHHHHHHHl...",
    "...HhHHHHHHHl...",
    "....HHHHHHHl....",
    "....HHHHHHHl....",
    "....HHHHHHHl....",
    ".....lllllll....",
    "................",
    "................",
    "................",
]
M_BOW = [
    "................",
    "..........hHH...",
    "........hH...s..",
    ".......hH...s...",
    "......hH...s....",
    ".....hH...s.....",
    ".....H...s......",
    "....hH..s.......",
    "....H..s........",
    "....H.s.........",
    "....Hs..........",
    "...ls...........",
    "..ls............",
    "................",
    "................",
    "................",
]
M_ARROW = [
    "................",
    "............HHH.",
    ".............HH.",
    "............h.H.",
    "...........h....",
    "..........h.....",
    ".........h......",
    "........h.......",
    ".......h........",
    "......h.........",
    ".....h..........",
    "..ff.h..........",
    "..fff...........",
    "...ff...........",
    "................",
    "................",
]
M_FEATHER = [
    "................",
    "...........HH...",
    "..........HHHH..",
    ".........HHHHH..",
    "........HHHHHh..",
    ".......HHHHHh...",
    "......HHHHHh....",
    ".....HHHHHh.....",
    "....HHHHhh......",
    "....HHhh........",
    "...lh...........",
    "..l.............",
    ".l..............",
    "................",
    "................",
    "................",
]
M_STRING = [
    "................",
    "................",
    "...........HH...",
    "..........H..H..",
    ".........H....H.",
    "........H.......",
    ".......H........",
    "......H.........",
    ".....H..........",
    "....H...........",
    "...H............",
    "..H.H...........",
    ".H...H..........",
    "................",
    "................",
    "................",
]
M_BONE = [
    "................",
    "................",
    "...........HH...",
    "..........HHHH..",
    "...........HHH..",
    "..........HH....",
    ".........HH.....",
    "........HH......",
    ".......HH.......",
    "......HH........",
    ".....HH.........",
    "..HHHH..........",
    "..HHHH..........",
    "...HH...........",
    "................",
    "................",
]
M_POWDER = [
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    ".......h........",
    "......hHh..h....",
    ".....hHHHhhHh...",
    "....hHHlHHHHHh..",
    "...hHHHHHlHHHHh.",
    "..hHlHHHHHHHlHH.",
    "..llllllllllll..",
    "................",
    "................",
    "................",
]
M_SEEDS = [
    "................",
    "................",
    "................",
    "................",
    "......H...H.....",
    "....H...H....H..",
    "......H....H....",
    "...H....H.......",
    ".......H...H.H..",
    ".....H.....H....",
    "..H.....H.......",
    "......H....H....",
    "................",
    "................",
    "................",
    "................",
]
M_WHEAT = [
    "................",
    "..........H.H...",
    ".........HhH....",
    "........HhHh.H..",
    ".......HhHhHH...",
    "......HhHhH.....",
    ".....hHhH.......",
    "....s.s.........",
    "...s.s..........",
    "..s.s...........",
    ".s.s............",
    ".s..............",
    "................",
    "................",
    "................",
    "................",
]
M_SHEARS = [
    "................",
    "................",
    "...........H....",
    "..........HH....",
    ".........HH.H...",
    "........HH.H....",
    ".......HHHH.....",
    "......HHH.......",
    ".....rrH........",
    "....rr.rr.......",
    "...rr...rr......",
    "...r.....r......",
    "...rr...rr......",
    "....rrrrr.......",
    "................",
    "................",
]
M_FLINT_STEEL = [
    "................",
    "................",
    "..HHHHH.........",
    ".H.....H........",
    ".H.....H........",
    "..H...H.........",
    "...HHH..........",
    "................",
    "..........fff...",
    ".........fFFFf..",
    "........fFFFFf..",
    "........fFFFf...",
    ".........fff....",
    "................",
    "................",
    "................",
]
M_HELMET = [
    "................",
    "................",
    "................",
    "....hhhhhhhh....",
    "...hHHHHHHHHh...",
    "..hHHHHHHHHHHh..",
    "..HHHHHHHHHHHl..",
    "..HHl......HHl..",
    "..HHl......HHl..",
    "..ll........ll..",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
]
M_CHEST = [
    "................",
    "..hhh......hhh..",
    ".hHHHh....hHHHh.",
    ".HHHHHhhhhHHHHl.",
    ".HHHHHHHHHHHHHl.",
    "..lHHHHHHHHHHl..",
    "...HHHHHHHHHl...",
    "...HHHHHHHHHl...",
    "...HHHHHHHHHl...",
    "...HHHHHHHHHl...",
    "...HHHHHHHHHl...",
    "...lllllllllll..",
    "................",
    "................",
    "................",
    "................",
]
M_LEGS = [
    "................",
    "................",
    "...hhhhhhhhhh...",
    "...HHHHHHHHHl...",
    "...HHHHHHHHHl...",
    "...HHHl..HHHl...",
    "...HHHl..HHHl...",
    "...HHHl..HHHl...",
    "...HHHl..HHHl...",
    "...HHHl..HHHl...",
    "...HHHl..HHHl...",
    "...lll...lll....",
    "................",
    "................",
    "................",
    "................",
]
M_BOOTS = [
    "................",
    "................",
    "................",
    "................",
    "................",
    "...hhh....hhh...",
    "...HHl....HHl...",
    "...HHl....HHl...",
    "...HHl....HHl...",
    "..HHHl...HHHl...",
    ".HHHHl..HHHHl...",
    ".lllll..lllll...",
    "................",
    "................",
    "................",
    "................",
]
M_BOWL = [
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "..hhhhhhhhhhhh..",
    "..HFFFFFFFFFFH..",
    "..HHFFFFFFFFHH..",
    "...HHHHHHHHHl...",
    "....lHHHHHll....",
    ".....llllll.....",
    "................",
    "................",
    "................",
    "................",
]
M_EGG = [
    "................",
    "................",
    "................",
    "......hhh.......",
    ".....hHHHh......",
    "....hHHHHHh.....",
    "....HHHHHHHl....",
    "...hHHHHHHHl....",
    "...HHHHHHHHHl...",
    "...HHHHHHHHHl...",
    "....HHHHHHHl....",
    ".....lllll......",
    "................",
    "................",
    "................",
    "................",
]
M_SLIMEBALL = M_LUMP

def mat_colors(hi, lo, extra=None):
    d = {"H": hi, "h": shade(hi, 1.12), "l": lo}
    if extra:
        d.update(extra)
    return d

def build_icons():
    # Tools
    for m in ["wood", "stone", "iron", "gold", "diamond"]:
        for k in ["pickaxe", "axe", "shovel", "sword", "hoe"]:
            reg_icon("%s_%s" % (m, k), icon_tool(k, m))
    reg_icon("stick", icon_from_mask(M_STICK, {"h": STICK[0], "l": STICK[1]}))
    reg_icon("coal", icon_from_mask(M_LUMP, mat_colors((50, 50, 52), (28, 28, 30))))
    reg_icon("charcoal", icon_from_mask(M_LUMP, mat_colors((70, 58, 46), (40, 32, 26))))
    reg_icon("iron_ingot", icon_from_mask(M_INGOT, mat_colors((220, 220, 224), (150, 150, 156))))
    reg_icon("gold_ingot", icon_from_mask(M_INGOT, mat_colors((250, 220, 70), (190, 140, 30))))
    reg_icon("diamond", icon_from_mask(M_GEM, mat_colors((120, 240, 230), (40, 170, 160))))
    reg_icon("redstone", icon_from_mask(M_POWDER, mat_colors((220, 30, 30), (130, 10, 10))))
    reg_icon("flint", icon_from_mask(M_LUMP, mat_colors((70, 70, 74), (40, 40, 44))))
    reg_icon("string", icon_from_mask(M_STRING, {"H": (236, 236, 236)}))
    reg_icon("feather", icon_from_mask(M_FEATHER, mat_colors((240, 240, 236), (190, 190, 186))))
    reg_icon("gunpowder", icon_from_mask(M_POWDER, mat_colors((110, 110, 110), (66, 66, 66))))
    reg_icon("bone", icon_from_mask(M_BONE, {"H": (236, 232, 220)}))
    reg_icon("bonemeal", icon_from_mask(M_POWDER, mat_colors((236, 236, 236), (190, 190, 200))))
    reg_icon("rotten_flesh", icon_from_mask(M_MEAT, mat_colors((150, 90, 60), (90, 120, 50), {"w": (90, 120, 50), "W": (60, 90, 40)})))
    reg_icon("leather", icon_from_mask(M_LUMP, mat_colors((150, 90, 50), (100, 60, 30))))
    reg_icon("wheat", icon_from_mask(M_WHEAT, mat_colors((220, 190, 80), (170, 140, 50), {"s": (180, 150, 60)})))
    reg_icon("seeds", icon_from_mask(M_SEEDS, {"H": (90, 160, 50)}))
    reg_icon("bread", icon_from_mask(M_BREAD, mat_colors((200, 140, 60), (140, 90, 40))))
    reg_icon("apple", icon_from_mask(M_APPLE, mat_colors((220, 30, 30), (140, 16, 16), {"s": (100, 70, 30), "g": (70, 150, 40)})))
    reg_icon("raw_porkchop", icon_from_mask(M_MEAT, mat_colors((240, 150, 150), (190, 100, 100), {"w": (250, 240, 230), "W": (220, 210, 200)})))
    reg_icon("cooked_porkchop", icon_from_mask(M_MEAT, mat_colors((200, 130, 80), (140, 80, 40), {"w": (250, 240, 230), "W": (220, 210, 200)})))
    reg_icon("raw_beef", icon_from_mask(M_MEAT, mat_colors((200, 50, 50), (140, 30, 30), {"w": (250, 240, 230), "W": (220, 210, 200)})))
    reg_icon("steak", icon_from_mask(M_MEAT, mat_colors((130, 80, 40), (80, 46, 20), {"w": (250, 240, 230), "W": (220, 210, 200)})))
    reg_icon("raw_chicken", icon_from_mask(M_MEAT, mat_colors((240, 200, 180), (200, 160, 140), {"w": (250, 240, 230), "W": (220, 210, 200)})))
    reg_icon("cooked_chicken", icon_from_mask(M_MEAT, mat_colors((210, 150, 80), (150, 100, 40), {"w": (250, 240, 230), "W": (220, 210, 200)})))
    reg_icon("mushroom_stew", icon_from_mask(M_BOWL, mat_colors((130, 90, 50), (90, 60, 30), {"F": (180, 130, 90)})))
    reg_icon("bowl", icon_from_mask(M_BOWL, mat_colors((130, 90, 50), (90, 60, 30))))
    reg_icon("egg", icon_from_mask(M_EGG, mat_colors((236, 220, 190), (190, 170, 140))))
    reg_icon("bucket", icon_from_mask(M_BUCKET, mat_colors((200, 200, 206), (140, 140, 146), {"F": (60, 60, 64)})))
    reg_icon("water_bucket", icon_from_mask(M_BUCKET, mat_colors((200, 200, 206), (140, 140, 146), {"F": (50, 90, 230)})))
    reg_icon("lava_bucket", icon_from_mask(M_BUCKET, mat_colors((200, 200, 206), (140, 140, 146), {"F": (250, 140, 20)})))
    reg_icon("milk_bucket", icon_from_mask(M_BUCKET, mat_colors((200, 200, 206), (140, 140, 146), {"F": (250, 250, 250)})))
    reg_icon("bow", icon_from_mask(M_BOW, mat_colors((140, 100, 50), (96, 66, 32), {"s": (230, 230, 230)})))
    reg_icon("arrow", icon_from_mask(M_ARROW, mat_colors((200, 200, 200), (100, 100, 100), {"h": STICK[0], "f": (240, 240, 240)})))
    reg_icon("flint_and_steel", icon_from_mask(M_FLINT_STEEL, {"H": (200, 200, 206), "f": (60, 60, 64), "F": (90, 90, 94)}))
    reg_icon("shears", icon_from_mask(M_SHEARS, {"H": (220, 220, 226), "r": (150, 30, 30)}))
    for m, (hi, lo) in [("leather", ((150, 90, 50), (100, 60, 30))), ("iron", MAT["iron"]), ("gold", MAT["gold"]), ("diamond", MAT["diamond"])]:
        reg_icon("%s_helmet" % m, icon_from_mask(M_HELMET, mat_colors(hi, lo)))
        reg_icon("%s_chestplate" % m, icon_from_mask(M_CHEST, mat_colors(hi, lo)))
        reg_icon("%s_leggings" % m, icon_from_mask(M_LEGS, mat_colors(hi, lo)))
        reg_icon("%s_boots" % m, icon_from_mask(M_BOOTS, mat_colors(hi, lo)))
    reg_icon("brick", icon_from_mask(M_INGOT, mat_colors((170, 80, 60), (120, 50, 36))))
    reg_icon("clay_ball", icon_from_mask(M_LUMP, mat_colors((170, 176, 190), (120, 126, 140))))
    reg_icon("paper", icon_from_mask(M_INGOT, mat_colors((240, 240, 234), (200, 200, 194))))
    reg_icon("sugar", icon_from_mask(M_POWDER, mat_colors((250, 250, 250), (200, 200, 210))))
    reg_icon("pumpkin_pie", icon_from_mask(M_BREAD, mat_colors((220, 140, 40), (150, 90, 30))))

# Block item icons: computed after textures; names "blk_<texname>" are made on demand
BLOCK_ICON_SPECS = []   # (name, kind, top, side, front)

def block_icon(name, top, side=None, front=None, flat=False):
    BLOCK_ICON_SPECS.append((name, top, side or top, front or side or top, flat))

def build_block_icons():
    tx = lambda n: TEX[TEXIDX[n]][1]
    for (name, top, side, front, flat) in BLOCK_ICON_SPECS:
        if flat:
            reg_icon(name, tx(top).image())
        else:
            reg_icon(name, outline(iso_cube(tx(top), tx(front), tx(side)), (20, 20, 20, 255)) if False else iso_cube(tx(top), tx(front), tx(side)))

# ----------------------------------------------------------------------------
# Palettes / quantization
# ----------------------------------------------------------------------------

def rgb555(c):
    return (c[0] >> 3) | ((c[1] >> 3) << 5) | ((c[2] >> 3) << 10)

def build_palette(images, ncol=255):
    """images: list of RGBA PIL images. Returns palette list (index0 = transparent)."""
    opaque = []
    for im in images:
        for p in im.getdata():
            if p[3] > 0:
                opaque.append(p[:3])
    # unique
    uniq = list(set(opaque))
    mosaic = Image.new("RGB", (len(opaque), 1))
    mosaic.putdata(opaque)
    q = mosaic.quantize(colors=ncol, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = q.getpalette()[:ncol * 3]
    colors = [(pal[i * 3], pal[i * 3 + 1], pal[i * 3 + 2]) for i in range(ncol)]
    return [(255, 0, 255)] + colors

def nearest(colors, c, cache):
    k = c
    if k in cache:
        return cache[k]
    best = None; bi = 1
    for i in range(1, len(colors)):
        p = colors[i]
        d = (p[0] - c[0]) ** 2 * 3 + (p[1] - c[1]) ** 2 * 4 + (p[2] - c[2]) ** 2 * 2
        if best is None or d < best:
            best = d; bi = i
    cache[k] = bi
    return bi

def index_image(im, colors, cache):
    out = bytearray()
    for p in im.getdata():
        out.append(0 if p[3] == 0 else nearest(colors, p[:3], cache))
    return out

# ----------------------------------------------------------------------------
# Font
# ----------------------------------------------------------------------------

def build_font():
    # The glyphs come from libnds' default font (part of the BlocksDS sources).
    # data/font.bin is committed, so this step is skipped if the sources are absent.
    src = os.environ.get("LIBNDS_FONT_PNG", "/opt/src/blocksds/libs/libnds/graphics/default_font.png")
    if not os.path.exists(src):
        print("font source not found, keeping existing data/font.bin")
        return
    im = Image.open(src).convert("L")
    data = bytearray()
    widths = bytearray()
    for ch in range(96):
        gx = (ch % 32) * 8; gy = (ch // 32) * 8
        rows = []
        maxx = -1; minx = 8
        for y in range(8):
            b = 0
            for x in range(8):
                if im.getpixel((gx + x, gy + y)) > 128:
                    b |= (1 << x)
                    maxx = max(maxx, x); minx = min(minx, x)
            rows.append(b)
        if maxx < 0:
            w = 4; shift = 0
        else:
            shift = minx
            w = maxx - minx + 2
        rows = [(r >> shift) for r in rows]
        data += bytes(rows)
        widths.append(w)
    with open(os.path.join(DATA, "font.bin"), "wb") as f:
        f.write(bytes(data) + bytes(widths))

# ----------------------------------------------------------------------------
# Sounds (8-bit signed PCM, 16384 Hz)
# ----------------------------------------------------------------------------

SR = 16384
SOUNDS = []

def snd(name, samples):
    out = bytearray()
    for s in samples:
        v = int(max(-1.0, min(1.0, s)) * 120)
        out.append(v & 0xFF)
    while len(out) % 4:
        out.append(0)
    SOUNDS.append((name, bytes(out)))

def env(n, a=0.005, d=None, curve=2.0):
    a_n = max(1, int(a * SR))
    for i in range(n):
        if i < a_n:
            yield i / a_n
        else:
            yield max(0.0, 1 - (i - a_n) / max(1, n - a_n)) ** curve

def lowpass(xs, k):
    y = 0.0; out = []
    for x in xs:
        y += (x - y) * k
        out.append(y)
    return out

def noise_burst(dur, k, seed, gain=1.0, curve=2.0):
    rng = random.Random(seed)
    n = int(dur * SR)
    xs = [rng.uniform(-1, 1) for _ in range(n)]
    xs = lowpass(xs, k)
    m = max(1e-6, max(abs(v) for v in xs))
    return [v / m * e * gain for v, e in zip(xs, env(n, 0.002, curve=curve))]

def tone(dur, f0, f1, wave="sin", gain=1.0, curve=1.5, vib=0.0):
    n = int(dur * SR)
    out = []; ph = 0.0
    for i, e in zip(range(n), env(n, 0.004, curve=curve)):
        t = i / n
        f = f0 + (f1 - f0) * t
        f *= 1 + vib * math.sin(i / SR * 2 * math.pi * 7)
        ph += f / SR
        x = ph % 1.0
        if wave == "sin":
            v = math.sin(2 * math.pi * ph)
        elif wave == "saw":
            v = 2 * x - 1
        elif wave == "sq":
            v = 1 if x < 0.5 else -1
        else:
            v = 4 * abs(x - 0.5) - 1
        out.append(v * e * gain)
    return out

def mixs(*parts):
    n = max(len(p) for p in parts)
    out = [0.0] * n
    for p in parts:
        for i, v in enumerate(p):
            out[i] += v
    m = max(1e-6, max(abs(v) for v in out))
    if m > 1:
        out = [v / m for v in out]
    return out

def concat(*parts):
    out = []
    for p in parts:
        out += p
    return out

def silence(d):
    return [0.0] * int(d * SR)

def build_sounds():
    # dig / break sounds per material
    snd("dig_stone", noise_burst(0.16, 0.55, 1, 0.9, 3))
    snd("dig_wood", mixs(noise_burst(0.14, 0.25, 2, 0.6, 3), tone(0.12, 180, 120, "tri", 0.6, 3)))
    snd("dig_grass", noise_burst(0.18, 0.18, 3, 0.9, 2.5))
    snd("dig_sand", noise_burst(0.2, 0.35, 4, 0.7, 1.5))
    snd("dig_glass", mixs(noise_burst(0.3, 0.9, 5, 0.6, 3), tone(0.25, 2400, 1800, "sin", 0.3, 3), tone(0.2, 3100, 2900, "sin", 0.25, 4)))
    snd("dig_cloth", noise_burst(0.14, 0.1, 6, 0.7, 2))
    snd("step", noise_burst(0.07, 0.2, 7, 0.5, 3))
    snd("pop", tone(0.09, 600, 1300, "sin", 0.7, 2))
    snd("click", tone(0.03, 1800, 1500, "sq", 0.4, 3))
    snd("hurt", mixs(tone(0.18, 260, 180, "saw", 0.6, 2), noise_burst(0.12, 0.3, 8, 0.4, 3)))
    snd("splash", noise_burst(0.35, 0.45, 9, 0.8, 2))
    snd("explode", mixs(noise_burst(0.9, 0.06, 10, 1.0, 1.6), tone(0.6, 90, 30, "sin", 0.8, 2)))
    snd("fuse", [v * (i / (0.8 * SR)) for i, v in enumerate(noise_burst(0.8, 0.8, 11, 0.6, 0.2))])
    snd("bow", mixs(tone(0.2, 300, 260, "tri", 0.6, 2), noise_burst(0.1, 0.4, 12, 0.3, 3)))
    snd("hit", mixs(noise_burst(0.08, 0.3, 13, 0.7, 3), tone(0.06, 160, 120, "sin", 0.6, 3)))
    snd("eat", concat(noise_burst(0.07, 0.5, 14, 0.6, 3), silence(0.05), noise_burst(0.07, 0.5, 15, 0.6, 3)))
    snd("burp", tone(0.25, 140, 100, "saw", 0.5, 1.5, vib=0.08))
    snd("zombie", mixs(tone(0.6, 110, 85, "saw", 0.6, 1.2, vib=0.06), tone(0.6, 165, 128, "saw", 0.3, 1.2, vib=0.06)))
    snd("skeleton", concat(*[concat(noise_burst(0.03, 0.9, 20 + i, 0.6, 4), silence(0.04)) for i in range(4)]))
    snd("spider", noise_burst(0.35, 0.7, 30, 0.5, 1.0))
    snd("pig", mixs(tone(0.22, 330, 260, "saw", 0.5, 1.5, vib=0.12), noise_burst(0.22, 0.2, 31, 0.3, 2)))
    snd("cow", tone(0.7, 150, 120, "saw", 0.6, 1.0, vib=0.03))
    snd("sheep", tone(0.5, 420, 400, "saw", 0.5, 1.2, vib=0.18))
    snd("chicken", concat(tone(0.07, 900, 1200, "sq", 0.4, 2), silence(0.03), tone(0.06, 1000, 1300, "sq", 0.4, 2)))
    snd("door", mixs(noise_burst(0.15, 0.2, 32, 0.6, 2), tone(0.15, 220, 180, "tri", 0.4, 2)))
    snd("fire", noise_burst(0.3, 0.6, 33, 0.3, 1.0))
    # piano-ish note for ambient music (C5 ~ 523Hz)
    n = int(1.6 * SR)
    piano = []
    for i in range(n):
        t = i / SR
        e = math.exp(-t * 2.6) * min(1.0, i / (0.004 * SR))
        v = (math.sin(2 * math.pi * 523.25 * t) * 0.6 + math.sin(2 * math.pi * 1046.5 * t) * 0.25 * math.exp(-t * 3) +
             math.sin(2 * math.pi * 1569.8 * t) * 0.12 * math.exp(-t * 5))
        piano.append(v * e)
    snd("note", piano)

# ----------------------------------------------------------------------------
# Main
# ----------------------------------------------------------------------------

def c_ident(s):
    return s.upper()

def main():
    build_block_textures()
    nmain = len(TEX)
    anim = []
    for f in range(ANIM_FRAMES):
        anim.append(("water_f%d" % f, tex_water(f)))
    for f in range(ANIM_FRAMES):
        anim.append(("lava_f%d" % f, tex_lava(f)))
    for f in range(4):
        anim.append(("fire_f%d" % f, tex_fire(f)))
    # cloud texture 64x64 (stored separately)
    rng = R(999)
    cloud = Image.new("RGBA", (64, 64), (0, 0, 0, 0))
    grid = [[0] * 16 for _ in range(16)]
    for _ in range(36):
        cx, cy = rng.randrange(16), rng.randrange(16)
        w, h = rng.randint(2, 5), rng.randint(1, 3)
        for y in range(cy, cy + h):
            for x in range(cx, cx + w):
                grid[y % 16][x % 16] = 1
    for y in range(64):
        for x in range(64):
            if grid[y // 4][x // 4]:
                cloud.putpixel((x, y), (250, 250, 252, 255))

    # Block-world palette
    world_imgs = [t.image() for _, t in TEX] + [t.image() for _, t in anim] + [cloud]
    pal0 = build_palette(world_imgs)
    cache0 = {}
    blob = bytearray()
    for _, t in TEX:
        blob += index_image(t.image(), pal0, cache0)
    for _, t in anim:
        blob += index_image(t.image(), pal0, cache0)
    with open(os.path.join(DATA, "blocktex.bin"), "wb") as f:
        f.write(blob)
    with open(os.path.join(DATA, "cloudtex.bin"), "wb") as f:
        f.write(index_image(cloud, pal0, cache0))
    with open(os.path.join(DATA, "blockpal.bin"), "wb") as f:
        for c in pal0:
            f.write(struct.pack("<H", rgb555(c)))

    # Icons
    build_icons()
    nitem_icons = len(ICONS)
    # block icons
    define_block_icons()
    build_block_icons()
    icon_imgs = [im for _, im in ICONS]
    pal1 = build_palette(icon_imgs)
    cache1 = {}
    blob = bytearray()
    for _, im in ICONS:
        blob += index_image(im, pal1, cache1)
    with open(os.path.join(DATA, "icontex.bin"), "wb") as f:
        f.write(blob)
    with open(os.path.join(DATA, "iconpal.bin"), "wb") as f:
        for c in pal1:
            f.write(struct.pack("<H", rgb555(c)))
    # 16-bit icons for UI (bit 15 = opaque)
    blob = bytearray()
    for _, im in ICONS:
        for p in im.getdata():
            v = (rgb555(p[:3]) | 0x8000) if p[3] > 0 else 0
            blob += struct.pack("<H", v)
    with open(os.path.join(DATA, "icons16.bin"), "wb") as f:
        f.write(blob)

    build_font()
    build_sounds()
    blob = bytearray()
    table = []
    for name, data in SOUNDS:
        table.append((name, len(blob), len(data)))
        blob += data
    with open(os.path.join(DATA, "sfx.bin"), "wb") as f:
        f.write(blob)

    # Header
    with open(os.path.join(SRC, "assets_gen.h"), "w") as f:
        f.write("// Generated by tools/gen_assets.py - do not edit\n#pragma once\n\n")
        for i, (n, _) in enumerate(TEX):
            f.write("#define TEX_%s %d\n" % (c_ident(n), i))
        f.write("#define TEX_COUNT %d\n" % len(TEX))
        f.write("#define TEX_ANIM_WATER %d\n" % len(TEX))
        f.write("#define TEX_ANIM_LAVA %d\n" % (len(TEX) + ANIM_FRAMES))
        f.write("#define TEX_ANIM_FIRE %d\n" % (len(TEX) + ANIM_FRAMES * 2))
        f.write("#define TEX_ANIM_FRAMES %d\n" % ANIM_FRAMES)
        f.write("#define TEX_TOTAL %d\n\n" % (len(TEX) + len(anim)))
        for i, (n, _) in enumerate(ICONS):
            f.write("#define ICON_%s %d\n" % (c_ident(n), i))
        f.write("#define ICON_COUNT %d\n\n" % len(ICONS))
        for i, (n, off, ln) in enumerate(table):
            f.write("#define SFX_%s %d\n" % (c_ident(n), i))
        f.write("#define SFX_COUNT %d\n" % len(table))
        f.write("static const unsigned int sfx_table[][2] = {\n")
        for (n, off, ln) in table:
            f.write("    { %d, %d }, // %s\n" % (off, ln, n))
        f.write("};\n")
        f.write("#define SFX_RATE %d\n" % SR)

    # Previews
    sheet = Image.new("RGBA", (16 * 16, ((len(TEX) + 15) // 16) * 16), (255, 0, 255, 255))
    for i, (n, t) in enumerate(TEX):
        sheet.paste(t.image(), ((i % 16) * 16, (i // 16) * 16))
    sheet.resize((sheet.width * 3, sheet.height * 3), Image.NEAREST).save(os.path.join(PREVIEW, "blocks.png"))
    sheet = Image.new("RGBA", (16 * 16, ((len(ICONS) + 15) // 16) * 16), (90, 90, 120, 255))
    for i, (n, im) in enumerate(ICONS):
        sheet.paste(im, ((i % 16) * 16, (i // 16) * 16), im)
    sheet.resize((sheet.width * 3, sheet.height * 3), Image.NEAREST).save(os.path.join(PREVIEW, "icons.png"))
    print("textures:", len(TEX), "+anim", len(anim), " icons:", len(ICONS), " sounds:", len(SOUNDS),
          " sfx bytes:", sum(len(d) for _, d in SOUNDS))

def define_block_icons():
    B = block_icon
    B("blk_grass", "grass_top", "grass_side")
    B("blk_dirt", "dirt"); B("blk_stone", "stone"); B("blk_cobblestone", "cobblestone"); B("blk_bedrock", "bedrock")
    B("blk_sand", "sand"); B("blk_gravel", "gravel")
    B("blk_log_oak", "log_oak_top", "log_oak"); B("blk_leaves_oak", "leaves_oak"); B("blk_planks_oak", "planks_oak")
    B("blk_log_birch", "log_birch_top", "log_birch"); B("blk_leaves_birch", "leaves_birch"); B("blk_planks_birch", "planks_birch")
    B("blk_log_spruce", "log_spruce_top", "log_spruce"); B("blk_leaves_spruce", "leaves_spruce"); B("blk_planks_spruce", "planks_spruce")
    for o in ["coal_ore", "iron_ore", "gold_ore", "diamond_ore", "redstone_ore"]:
        B("blk_" + o, o)
    B("blk_glass", "glass"); B("blk_bricks", "bricks"); B("blk_sandstone", "sandstone_top", "sandstone")
    B("blk_snow", "snow"); B("blk_ice", "ice"); B("blk_clay", "clay"); B("blk_obsidian", "obsidian")
    B("blk_crafting_table", "crafting_top", "crafting_side", "crafting_front")
    B("blk_furnace", "furnace_top", "furnace_side", "furnace_front")
    B("blk_chest", "chest_top", "chest_side", "chest_front")
    B("blk_torch", "torch", flat=True)
    B("blk_flower_red", "flower_red", flat=True); B("blk_flower_yellow", "flower_yellow", flat=True)
    B("blk_tallgrass", "tallgrass", flat=True); B("blk_deadbush", "deadbush", flat=True)
    B("blk_sapling_oak", "sapling_oak", flat=True); B("blk_sapling_birch", "sapling_birch", flat=True); B("blk_sapling_spruce", "sapling_spruce", flat=True)
    B("blk_mushroom_red", "mushroom_red", flat=True); B("blk_mushroom_brown", "mushroom_brown", flat=True)
    B("blk_cactus", "cactus_top", "cactus_side")
    for nm, _ in WOOL_COLORS:
        B("blk_wool_" + nm, "wool_" + nm)
    B("blk_tnt", "tnt_top", "tnt_side"); B("blk_bookshelf", "planks_oak", "bookshelf")
    B("blk_mossy_cobblestone", "mossy_cobblestone"); B("blk_stonebricks", "stonebricks")
    for b in ["iron_block", "gold_block", "diamond_block", "coal_block"]:
        B("blk_" + b, b)
    B("blk_ladder", "ladder", flat=True)
    B("blk_pumpkin", "pumpkin_top", "pumpkin_side", "pumpkin_face")
    B("blk_jackolantern", "pumpkin_top", "pumpkin_side", "jackolantern_face")
    B("blk_bed", "bed_head_top", flat=True)
    B("blk_door", "door_bottom", flat=True)
    B("blk_water", "water"); B("blk_lava", "lava")
    B("blk_farmland", "farmland_dry", "dirt")

if __name__ == "__main__":
    main()
