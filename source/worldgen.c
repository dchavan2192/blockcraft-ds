// Terrain generation: biomes, caves, ores, trees and decorations
#include <stdlib.h>
#include "worldgen.h"
#include "world.h"
#include "noise.h"

uint8_t *biome_map;

static Noise n_cont, n_detail, n_mount, n_temp, n_hum, n_cave1, n_cave2, n_cavern, n_soil;
static uint32_t gseed;

static uint32_t grng(void)
{
    gseed ^= gseed << 13;
    gseed ^= gseed >> 17;
    gseed ^= gseed << 5;
    return gseed;
}
static int grange(int n) { return (int)((grng() >> 4) % (uint32_t)n); }

// Position hash for deterministic per-column decisions
static uint32_t phash(int x, int z, uint32_t salt)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)z * 668265263u + salt * 2246822519u + gseed;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

const char *biome_name(int b)
{
    static const char *names[] = { "Ocean", "Beach", "Plains", "Forest", "Desert", "Snowy Taiga", "Mountains" };
    return names[b];
}

int worldgen_biome(int x, int z)
{
    if (!biome_map)
        return BIOME_PLAINS;
    return biome_map[((z & world.mask) << world.shift) + (x & world.mask)];
}

// Low resolution noise grids (every 4 blocks), interpolated per column
static int16_t *ng_cont, *ng_detail, *ng_mount, *ng_temp, *ng_hum;
static int ngw;

static void build_noise_grids(int ws)
{
    ngw = ws / 4;
    int n = ngw * ngw;
    ng_cont = malloc(n * 2); ng_detail = malloc(n * 2); ng_mount = malloc(n * 2);
    ng_temp = malloc(n * 2); ng_hum = malloc(n * 2);
    for (int gz = 0; gz < ngw; gz++) {
        for (int gx = 0; gx < ngw; gx++) {
            int x = gx * 4, z = gz * 4, i = gz * ngw + gx;
            ng_cont[i] = fbm2(&n_cont, x, z, 6, 3, ws);
            ng_detail[i] = fbm2(&n_detail, x, z, 4, 2, ws);
            ng_mount[i] = fbm2(&n_mount, x, z, 6, 2, ws);
            ng_temp[i] = fbm2(&n_temp, x, z, 6, 2, ws);
            ng_hum[i] = fbm2(&n_hum, x, z, 6, 2, ws);
        }
    }
}

static void free_noise_grids(void)
{
    free(ng_cont); free(ng_detail); free(ng_mount); free(ng_temp); free(ng_hum);
    ng_cont = ng_detail = ng_mount = ng_temp = ng_hum = NULL;
}

static inline int32_t grid_sample(const int16_t *g, int x, int z)
{
    int gx0 = (x >> 2) & (ngw - 1), gz0 = (z >> 2) & (ngw - 1);
    int gx1 = (gx0 + 1) & (ngw - 1), gz1 = (gz0 + 1) & (ngw - 1);
    int fx = (x & 3) << 10, fz = (z & 3) << 10;
    int32_t a = g[gz0 * ngw + gx0] + (((g[gz0 * ngw + gx1] - g[gz0 * ngw + gx0]) * fx) >> 12);
    int32_t b = g[gz1 * ngw + gx0] + (((g[gz1 * ngw + gx1] - g[gz1 * ngw + gx0]) * fx) >> 12);
    return a + (((b - a) * fz) >> 12);
}

static int terrain_height(int x, int z, int *biome_out)
{
    int32_t c = grid_sample(ng_cont, x, z);          // continents, -4096..4096
    int32_t d = grid_sample(ng_detail, x, z);        // local bumps
    int32_t m = grid_sample(ng_mount, x, z);
    int32_t t = grid_sample(ng_temp, x, z);
    int32_t hmd = grid_sample(ng_hum, x, z);

    int32_t h = (SEA_LEVEL + 3) * 4096 + c * 13 + d * 4;
    if (m > 1000) {
        int32_t mm = m - 1000;
        h += (int32_t)(((int64_t)mm * mm) >> 12) * 9;
    }
    if (c < -1300)
        h -= (-1300 - c) * 9;
    int hi = h >> 12;
    hi = iclamp(hi, 6, WORLD_H - 3);

    int biome;
    if (hi < SEA_LEVEL - 1)
        biome = BIOME_OCEAN;
    else if (hi <= SEA_LEVEL + 1 && c > -2600 && m < 1200)
        biome = BIOME_BEACH;
    else if (hi > SEA_LEVEL + 20)
        biome = BIOME_MOUNTAINS;
    else if (t < -1100)
        biome = BIOME_SNOWY;
    else if (t > 1000 && hmd < 300)
        biome = BIOME_DESERT;
    else if (hmd > 600)
        biome = BIOME_FOREST;
    else
        biome = BIOME_PLAINS;
    if (biome == BIOME_BEACH && t < -1100)
        biome = BIOME_SNOWY;
    if (biome == BIOME_OCEAN && t < -1400)
        biome = BIOME_OCEAN | 0x80; // frozen ocean flag
    *biome_out = biome;
    return hi;
}

static void place_vein(int x, int y, int z, int ore, int size)
{
    for (int i = 0; i < size; i++) {
        if (y > 0 && y < WORLD_H && world_get(x, y, z) == B_STONE)
            world_set_raw(x, y, z, ore);
        switch (grange(6)) {
            case 0: x++; break;
            case 1: x--; break;
            case 2: z++; break;
            case 3: z--; break;
            case 4: y++; break;
            default: y--; break;
        }
    }
}

static bool can_replace_for_tree(int id)
{
    return id == B_AIR || is_leaves(id) || id == B_TALLGRASS || id == B_SNOW || id == B_FLOWER_RED ||
           id == B_FLOWER_YELLOW || (id >= B_SAPLING_OAK && id <= B_SAPLING_SPRUCE);
}

static void (*tset)(int x, int y, int z, int id) = world_set_raw;

static void leaf(int x, int y, int z, int id)
{
    if (y >= WORLD_H)
        return;
    int cur = world_get(x, y, z);
    if (cur == B_AIR || cur == B_TALLGRASS)
        tset(x, y, z, id);
}

// Grows a tree using raw sets. Returns false if blocked.
static bool tree_raw(int x, int y, int z, int kind, uint32_t (*rnd)(void))
{
    int h;
    int logb, leafb;
    if (kind == 2) {
        h = 6 + (rnd() >> 4) % 4;
        logb = B_LOG_SPRUCE; leafb = B_LEAVES_SPRUCE;
    } else if (kind == 1) {
        h = 5 + (rnd() >> 4) % 3;
        logb = B_LOG_BIRCH; leafb = B_LEAVES_BIRCH;
    } else {
        h = 4 + (rnd() >> 4) % 3;
        logb = B_LOG_OAK; leafb = B_LEAVES_OAK;
    }
    if (y + h + 2 >= WORLD_H)
        return false;
    for (int i = 1; i <= h; i++)
        if (!can_replace_for_tree(world_get(x, y + i, z)))
            return false;
    if (kind == 2) {
        // Cone of leaves
        int top = y + h;
        leaf(x, top + 1, z, leafb);
        int i = 0;
        for (int ly = top; ly >= y + 2; ly--, i++) {
            int rad = (i == 0) ? 0 : ((i % 2) == 1 ? 1 : 2);
            if (ly == y + 2 && rad == 1)
                rad = 2;
            for (int dz = -rad; dz <= rad; dz++)
                for (int dx = -rad; dx <= rad; dx++) {
                    if (rad > 0 && iabs(dx) == rad && iabs(dz) == rad)
                        continue;
                    leaf(x + dx, ly, z + dz, leafb);
                }
        }
    } else {
        int top = y + h;
        for (int ly = top - 2; ly <= top + 1; ly++) {
            int rad = (ly >= top) ? 1 : 2;
            for (int dz = -rad; dz <= rad; dz++)
                for (int dx = -rad; dx <= rad; dx++) {
                    if (iabs(dx) == rad && iabs(dz) == rad && (ly == top + 1 || (rnd() & 1)))
                        continue;
                    leaf(x + dx, ly, z + dz, leafb);
                }
        }
    }
    for (int i = 1; i <= h; i++)
        tset(x, y + i, z, logb);
    tset(x, y, z, B_DIRT);
    return true;
}

static uint32_t tree_rng(void) { return grng(); }

void worldgen_grow_tree(int x, int y, int z, int kind)
{
    // Called at runtime by saplings: y = sapling y; ground is y-1
    tset = world_set;
    world_set(x, y, z, B_AIR);
    if (!tree_raw(x, y - 1, z, kind, rng_next))
        world_set(x, y, z, B_SAPLING_OAK + kind);
    tset = world_set_raw;
}

ARM_CODE void worldgen_generate(uint32_t seed, void (*progress)(const char *stage, int pct))
{
    int ws = world.size;
    gseed = seed ? seed : 1;
    noise_seed(&n_cont, seed);
    noise_seed(&n_detail, seed + 1);
    noise_seed(&n_mount, seed + 2);
    noise_seed(&n_temp, seed + 3);
    noise_seed(&n_hum, seed + 4);
    noise_seed(&n_cave1, seed + 5);
    noise_seed(&n_cave2, seed + 6);
    noise_seed(&n_cavern, seed + 7);
    noise_seed(&n_soil, seed + 8);

    free(biome_map);
    biome_map = malloc(ws * ws);
    uint8_t *hmap = malloc(ws * ws);

    // ---- Pass 1: terrain columns ----
    build_noise_grids(ws);
    for (int z = 0; z < ws; z++) {
        for (int x = 0; x < ws; x++) {
            int biome;
            int h = terrain_height(x, z, &biome);
            hmap[z * ws + x] = h;
            biome_map[z * ws + x] = biome;
            bool frozen = (biome & 0x80) != 0;
            biome &= 0x7F;
            biome_map[z * ws + x] = biome;
            int soil = 3 + (phash(x >> 2, z >> 2, 5) & 1);
            int top, filler;
            switch (biome) {
                case BIOME_OCEAN: {
                    int32_t sn = grid_sample(ng_detail, x + 64, z + 32);
                    top = filler = (sn > 1600) ? B_GRAVEL : B_SAND;
                    if (h < SEA_LEVEL - 6 && sn < -1800)
                        top = filler = B_CLAY;
                    break;
                }
                case BIOME_BEACH:
                    top = filler = B_SAND; break;
                case BIOME_DESERT:
                    top = filler = B_SAND; break;
                case BIOME_SNOWY:
                    top = B_SNOWY_GRASS; filler = B_DIRT; break;
                case BIOME_MOUNTAINS:
                    top = (h > SEA_LEVEL + 26) ? B_STONE : B_GRASS;
                    if (h > SEA_LEVEL + 29)
                        top = B_SNOWY_GRASS;
                    filler = (top == B_STONE) ? B_STONE : B_DIRT;
                    break;
                default:
                    top = B_GRASS; filler = B_DIRT; break;
            }
            if (h <= SEA_LEVEL && top == B_GRASS)
                top = B_DIRT;
            uint8_t *col = &world.blk[widx(x, 0, z)];
            for (int y = 0; y < WORLD_H; y++) {
                int id;
                if (y == 0)
                    id = B_BEDROCK;
                else if (y <= 3 && (phash(x, z, y) & 3) < (uint32_t)(4 - y))
                    id = B_BEDROCK;
                else if (y < h - soil)
                    id = (biome == BIOME_DESERT && y >= h - soil - 3) ? B_SANDSTONE : B_STONE;
                else if (y < h - 1)
                    id = filler;
                else if (y == h - 1)
                    id = top;
                else if (y <= SEA_LEVEL)
                    id = (frozen && y == SEA_LEVEL) ? B_ICE : B_WATER;
                else
                    id = B_AIR;
                col[y] = id;
            }
        }
        if (progress && (z & 7) == 0)
            progress("Shaping terrain", z * 100 / ws);
    }

    free_noise_grids();

    // ---- Pass 2: caves (3D noise sampled on a 4x4x4 grid, interpolated) ----
    int gw = ws / 4;          // grid cells per axis (wrapping)
    int gh = WORLD_H / 4 + 1;
    int16_t *g1 = malloc(gw * gw * gh * sizeof(int16_t));
    int16_t *g2 = malloc(gw * gw * gh * sizeof(int16_t));
    int16_t *g3 = malloc(gw * gw * gh * sizeof(int16_t));
    if (g1 && g2 && g3) {
        for (int gz = 0; gz < gw; gz++) {
            for (int gx = 0; gx < gw; gx++) {
                for (int gy = 0; gy < gh; gy++) {
                    int i = (gz * gw + gx) * gh + gy;
                    g1[i] = noise3(&n_cave1, gx * 4, gy * 8, gz * 4, 4, ws >> 4);
                    g2[i] = noise3(&n_cave2, gx * 4, gy * 8, gz * 4, 4, ws >> 4);
                    g3[i] = noise3(&n_cavern, gx * 4, gy * 4, gz * 4, 5, ws >> 5);
                }
            }
            if (progress && (gz & 3) == 0)
                progress("Carving caves", gz * 50 / gw);
        }
        for (int z = 0; z < ws; z++) {
            for (int x = 0; x < ws; x++) {
                int h = hmap[z * ws + x];
                int biome = biome_map[z * ws + x];
                bool wet = (biome == BIOME_OCEAN || biome == BIOME_BEACH || h <= SEA_LEVEL + 2);
                int maxy = wet ? h - 5 : h;
                if (maxy > WORLD_H - 1)
                    maxy = WORLD_H - 1;
                int gx0 = x >> 2, gz0 = z >> 2;
                int gx1 = (gx0 + 1) & (gw - 1), gz1 = (gz0 + 1) & (gw - 1);
                int fx = (x & 3) << 10, fz = (z & 3) << 10;
                int fx1 = 4096 - fx, fz1 = 4096 - fz;
                // bilinear weights for the four grid columns
                int w00 = (fx1 * fz1) >> 12, w10 = (fx * fz1) >> 12, w01 = (fx1 * fz) >> 12, w11 = (fx * fz) >> 12;
                int c00 = (gz0 * gw + gx0) * gh, c10 = (gz0 * gw + gx1) * gh;
                int c01 = (gz1 * gw + gx0) * gh, c11 = (gz1 * gw + gx1) * gh;
                uint8_t *col = &world.blk[widx(x, 0, z)];
                int ngy = (maxy >> 2) + 1;
                for (int gy = 0; gy < ngy && gy + 1 < gh; gy++) {
                    // values at this grid level and the next for each noise
                    int32_t a0 = (g1[c00 + gy] * w00 + g1[c10 + gy] * w10 + g1[c01 + gy] * w01 + g1[c11 + gy] * w11) >> 12;
                    int32_t a1 = (g1[c00 + gy + 1] * w00 + g1[c10 + gy + 1] * w10 + g1[c01 + gy + 1] * w01 + g1[c11 + gy + 1] * w11) >> 12;
                    int32_t b0 = (g2[c00 + gy] * w00 + g2[c10 + gy] * w10 + g2[c01 + gy] * w01 + g2[c11 + gy] * w11) >> 12;
                    int32_t b1 = (g2[c00 + gy + 1] * w00 + g2[c10 + gy + 1] * w10 + g2[c01 + gy + 1] * w01 + g2[c11 + gy + 1] * w11) >> 12;
                    int32_t d0 = (g3[c00 + gy] * w00 + g3[c10 + gy] * w10 + g3[c01 + gy] * w01 + g3[c11 + gy] * w11) >> 12;
                    int32_t d1 = (g3[c00 + gy + 1] * w00 + g3[c10 + gy + 1] * w10 + g3[c01 + gy + 1] * w01 + g3[c11 + gy + 1] * w11) >> 12;
                    int32_t da = (a1 - a0) >> 2, db = (b1 - b0) >> 2, dd = (d1 - d0) >> 2;
                    int32_t va = a0, vb = b0, vd = d0;
                    for (int k = 0; k < 4; k++, va += da, vb += db, vd += dd) {
                        int y = gy * 4 + k;
                        if (y < 1)
                            continue;
                        if (y >= maxy)
                            break;
                        int32_t aa = va >> 4, bb = vb >> 4;
                        int thr = (y > h - 7) ? 350 : 900;
                        bool carve = (aa * aa + bb * bb < thr) || (y < 24 && vd > 2300 - (24 - y) * 25);
                        if (!carve)
                            continue;
                        int id = col[y];
                        if (id == B_BEDROCK || id == B_WATER)
                            continue;
                        if (is_water(col[y + 1]))
                            continue;
                        col[y] = (y <= 6) ? B_LAVA : B_AIR;
                    }
                }
            }
            if (progress && (z & 7) == 0)
                progress("Carving caves", 50 + z * 50 / ws);
        }
    }
    free(g1);
    free(g2);
    free(g3);

    // Grass under exposed caves stays; dirt that became top after carving -> grass
    for (int z = 0; z < ws; z++) {
        for (int x = 0; x < ws; x++) {
            uint8_t *col = &world.blk[widx(x, 0, z)];
            int h = hmap[z * ws + x];
            if (h - 1 >= 0 && col[h - 1] == B_AIR) {
                // top carved, find new surface
                int y = h - 1;
                while (y > 0 && col[y] == B_AIR)
                    y--;
                if (col[y] == B_DIRT && y > SEA_LEVEL)
                    col[y] = B_GRASS;
            }
        }
    }

    // ---- Pass 3: ores and pockets ----
    int chunks = ws / 16;
    for (int cz = 0; cz < chunks; cz++) {
        for (int cx = 0; cx < chunks; cx++) {
            int bx = cx * 16, bz = cz * 16;
            struct { int id, tries, maxy, size; } ores[] = {
                { B_DIRT, 6, 56, 18 }, { B_GRAVEL, 4, 56, 16 },
                { B_COAL_ORE, 18, 56, 12 }, { B_IRON_ORE, 12, 44, 8 },
                { B_GOLD_ORE, 2, 26, 7 }, { B_REDSTONE_ORE, 5, 14, 7 },
                { B_DIAMOND_ORE, 1, 13, 6 },
            };
            for (unsigned o = 0; o < sizeof(ores) / sizeof(ores[0]); o++) {
                int tries = ores[o].tries;
                if (ores[o].id == B_DIAMOND_ORE && grange(3) == 0)
                    tries++;
                for (int t = 0; t < tries; t++) {
                    int x = bx + grange(16), z = bz + grange(16);
                    int y = 2 + grange(ores[o].maxy - 2);
                    place_vein(x, y, z, ores[o].id, ores[o].size);
                }
            }
        }
        if (progress)
            progress("Placing ores", cz * 100 / chunks);
    }

    // ---- Pass 4: surface decoration ----
    for (int z = 0; z < ws; z++) {
        for (int x = 0; x < ws; x++) {
            int biome = biome_map[z * ws + x];
            // find surface
            int y = WORLD_H - 2;
            uint8_t *col = &world.blk[widx(x, 0, z)];
            while (y > 0 && col[y] == B_AIR)
                y--;
            int ground = col[y];
            uint32_t r = phash(x, z, 77);
            bool on_grass = (ground == B_GRASS || ground == B_SNOWY_GRASS);
            // Trees on a coarse jittered grid so they don't overlap
            if (on_grass && (x % 3 == 0) && (z % 3 == 0)) {
                int chance = 0, kind = 0;
                switch (biome) {
                    case BIOME_FOREST: chance = 300; kind = (r & 0x300) == 0 ? 1 : 0; break;
                    case BIOME_PLAINS: chance = 18; kind = 0; break;
                    case BIOME_SNOWY: chance = 150; kind = 2; break;
                    case BIOME_MOUNTAINS: chance = 60; kind = 2; break;
                    default: break;
                }
                if ((int)(r % 1000) < chance) {
                    int ox = (r >> 12) % 2, oz = (r >> 14) % 2;
                    int tx = x + ox, tz = z + oz;
                    int ty = WORLD_H - 2;
                    while (ty > 0 && world_get(tx, ty, tz) == B_AIR)
                        ty--;
                    int g = world_get(tx, ty, tz);
                    if (g == B_GRASS || g == B_SNOWY_GRASS)
                        tree_raw(tx, ty, tz, kind, tree_rng);
                    continue;
                }
            }
            if (col[y + 1] != B_AIR)
                continue;
            if (on_grass) {
                uint32_t r2 = r >> 10;
                if (biome == BIOME_PLAINS || biome == BIOME_FOREST) {
                    if (r2 % 100 < 6)
                        col[y + 1] = B_TALLGRASS;
                    else if (r2 % 100 < 8)
                        col[y + 1] = (r2 & 0x1000) ? B_FLOWER_RED : B_FLOWER_YELLOW;
                    else if (r2 % 2400 == 7)
                        col[y + 1] = B_PUMPKIN + (r2 >> 4) % 4;
                } else if (biome == BIOME_SNOWY && r2 % 100 < 4) {
                    col[y + 1] = B_TALLGRASS;
                } else if (biome == BIOME_MOUNTAINS && r2 % 100 < 5) {
                    col[y + 1] = B_TALLGRASS;
                }
            } else if (ground == B_SAND && biome == BIOME_DESERT) {
                uint32_t r2 = r >> 10;
                if (r2 % 200 < 2) {
                    // cactus needs free sides
                    bool ok = true;
                    for (int f = 0; f < 6; f++) {
                        if (f == FACE_U || f == FACE_D)
                            continue;
                        if (world_get(x + face_dx[f], y + 1, z + face_dz[f]) != B_AIR)
                            ok = false;
                    }
                    if (ok) {
                        int hh = 1 + (r2 >> 8) % 3;
                        for (int i = 1; i <= hh; i++)
                            col[y + i] = B_CACTUS;
                    }
                } else if (r2 % 200 < 5) {
                    col[y + 1] = B_DEADBUSH;
                }
            }
        }
        if (progress && (z & 7) == 0)
            progress("Growing trees", z * 100 / ws);
    }

    // Mushrooms in dark caves
    for (int i = 0; i < ws * ws / 64; i++) {
        int x = grange(ws), z = grange(ws), y = 8 + grange(30);
        if (world_get(x, y, z) == B_AIR && world_get(x, y - 1, z) == B_STONE)
            world_set_raw(x, y, z, grange(2) ? B_MUSHROOM_BROWN : B_MUSHROOM_RED);
    }

    // Spawn point: search outward from center for dry land
    int cxw = ws / 2, czw = ws / 2;
    world.spawn_x = cxw; world.spawn_z = czw; world.spawn_y = SEA_LEVEL + 2;
    for (int r = 0; r < ws / 2; r += 2) {
        bool found = false;
        for (int dz = -r; dz <= r && !found; dz += 2) {
            for (int dx = -r; dx <= r && !found; dx += 2) {
                if (iabs(dx) != r && iabs(dz) != r)
                    continue;
                int x = cxw + dx, z = czw + dz;
                int y = WORLD_H - 2;
                while (y > 0 && !block_solid(world_get(x, y, z)) && !block_liquid(world_get(x, y, z)))
                    y--;
                int g = world_get(x, y, z);
                if ((g == B_GRASS || g == B_SAND || g == B_SNOWY_GRASS) && y > SEA_LEVEL &&
                    world_get(x, y + 1, z) == B_AIR && world_get(x, y + 2, z) == B_AIR) {
                    world.spawn_x = x & world.mask;
                    world.spawn_z = z & world.mask;
                    world.spawn_y = y + 1;
                    found = true;
                }
            }
        }
        if (found)
            break;
    }
    free(hmap);
}
