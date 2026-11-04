// Villages: structure generation, villager records, professions and trading
#include <stdio.h>
#include <stdlib.h>
#include "village.h"
#include "world.h"
#include "worldgen.h"
#include "blocks.h"
#include "inventory.h"
#include "entity.h"
#include "player.h"
#include "sound.h"
#include "game.h"
#include "assets_gen.h"

Village villages[MAX_VILLAGES];

// ---------------------------------------------------------------------------
// Professions and trades
// ---------------------------------------------------------------------------

const ProfInfo prof_info[PROF_COUNT] = {
    [PROF_FARMER] =      { "Farmer",      RGB15(22, 16, 10), RGB15(31, 27, 12), HAT_BRIM, 0 },
    [PROF_LIBRARIAN] =   { "Librarian",   RGB15(30, 30, 28), RGB15(24, 6, 6),   HAT_CAP,  0 },
    [PROF_ARMORER] =     { "Armorer",     RGB15(12, 12, 14), RGB15(6, 6, 7),    HAT_NONE, 1 },
    [PROF_TOOLSMITH] =   { "Toolsmith",   RGB15(16, 14, 12), RGB15(12, 8, 5),   HAT_NONE, 1 },
    [PROF_WEAPONSMITH] = { "Weaponsmith", RGB15(10, 10, 12), RGB15(4, 4, 4),    HAT_NONE, 1 },
    [PROF_BUTCHER] =     { "Butcher",     RGB15(29, 29, 29), RGB15(26, 9, 9),   HAT_NONE, 1 },
    [PROF_SHEPHERD] =    { "Shepherd",    RGB15(25, 25, 23), RGB15(16, 11, 7),  HAT_CAP,  0 },
    [PROF_FLETCHER] =    { "Fletcher",    RGB15(14, 22, 10), RGB15(9, 17, 7),   HAT_CAP,  0 },
    [PROF_MASON] =       { "Mason",       RGB15(18, 18, 18), RGB15(12, 10, 8),  HAT_NONE, 1 },
    [PROF_CLERIC] =      { "Cleric",      RGB15(18, 8, 24),  RGB15(15, 6, 21),  HAT_HOOD, 0 },
};

#define EM I_EMERALD
// player gives n of item for em emeralds / pays em emeralds for n of item
#define BUY(item, n, em, uses, xp, lv)  { item, 0, EM, n, 0, em, uses, xp, lv }
#define SELL(em, item, n, uses, xp, lv) { EM, 0, item, em, 0, n, uses, xp, lv }
#define TWO(a, an, b, bn, item, n, uses, xp, lv) { a, b, item, an, bn, n, uses, xp, lv }

// Two trades per level, Novice (1) to Master (5)
static const Trade trades[PROF_COUNT][TRADES_PER_PROF] = {
    [PROF_FARMER] = {
        BUY(I_WHEAT, 20, 1, 16, 2, 1), SELL(1, I_BREAD, 6, 16, 1, 1),
        BUY(B_PUMPKIN, 6, 1, 12, 10, 2), SELL(1, I_PUMPKIN_PIE, 4, 12, 5, 2),
        SELL(1, I_APPLE, 4, 16, 10, 3), BUY(I_EGG, 12, 1, 12, 20, 3),
        SELL(1, I_SEEDS, 16, 12, 15, 4), SELL(2, I_BONEMEAL, 12, 12, 15, 4),
        SELL(2, B_JACKOLANTERN, 2, 12, 30, 5), SELL(4, I_MUSHROOM_STEW, 1, 12, 30, 5),
    },
    [PROF_LIBRARIAN] = {
        BUY(I_PAPER, 24, 1, 16, 2, 1), SELL(9, B_BOOKSHELF, 1, 12, 1, 1),
        BUY(I_LEATHER, 4, 1, 12, 10, 2), SELL(1, B_GLASS, 4, 12, 5, 2),
        BUY(I_FEATHER, 16, 1, 16, 20, 3), SELL(1, B_TORCH, 16, 12, 10, 3),
        SELL(3, B_LADDER, 8, 12, 15, 4), SELL(4, B_BOOKSHELF, 1, 12, 15, 4),
        SELL(2, B_GLASS, 16, 12, 30, 5), TWO(I_PAPER, 8, EM, 3, B_BOOKSHELF, 2, 12, 30, 5),
    },
    [PROF_ARMORER] = {
        BUY(I_COAL, 15, 1, 16, 2, 1), SELL(5, ARMOR_ID(AM_IRON, AS_HEAD), 1, 12, 1, 1),
        BUY(I_IRON_INGOT, 4, 1, 12, 10, 2), SELL(9, ARMOR_ID(AM_IRON, AS_CHEST), 1, 12, 5, 2),
        SELL(7, ARMOR_ID(AM_IRON, AS_LEGS), 1, 12, 10, 3), SELL(4, ARMOR_ID(AM_IRON, AS_FEET), 1, 12, 10, 3),
        BUY(I_DIAMOND, 1, 1, 12, 30, 4), SELL(13, ARMOR_ID(AM_DIAMOND, AS_HEAD), 1, 3, 15, 4),
        SELL(21, ARMOR_ID(AM_DIAMOND, AS_CHEST), 1, 3, 30, 5), SELL(17, ARMOR_ID(AM_DIAMOND, AS_LEGS), 1, 3, 30, 5),
    },
    [PROF_TOOLSMITH] = {
        BUY(I_COAL, 15, 1, 16, 2, 1), SELL(1, TOOL_ID(MAT_STONE, TK_PICKAXE), 1, 12, 1, 1),
        BUY(I_IRON_INGOT, 4, 1, 12, 10, 2), SELL(1, TOOL_ID(MAT_STONE, TK_SHOVEL), 1, 12, 5, 2),
        BUY(I_FLINT, 30, 1, 12, 20, 3), SELL(3, TOOL_ID(MAT_IRON, TK_PICKAXE), 1, 12, 10, 3),
        BUY(I_DIAMOND, 1, 1, 12, 30, 4), SELL(4, TOOL_ID(MAT_DIAMOND, TK_HOE), 1, 3, 15, 4),
        SELL(13, TOOL_ID(MAT_DIAMOND, TK_PICKAXE), 1, 3, 30, 5), SELL(8, TOOL_ID(MAT_DIAMOND, TK_SHOVEL), 1, 3, 30, 5),
    },
    [PROF_WEAPONSMITH] = {
        BUY(I_COAL, 15, 1, 16, 2, 1), SELL(3, TOOL_ID(MAT_IRON, TK_AXE), 1, 12, 1, 1),
        BUY(I_IRON_INGOT, 4, 1, 12, 10, 2), SELL(2, TOOL_ID(MAT_IRON, TK_SWORD), 1, 12, 5, 2),
        BUY(I_FLINT, 24, 1, 12, 20, 3), SELL(1, TOOL_ID(MAT_STONE, TK_SWORD), 1, 12, 10, 3),
        BUY(I_DIAMOND, 1, 1, 12, 30, 4), SELL(12, TOOL_ID(MAT_DIAMOND, TK_AXE), 1, 3, 15, 4),
        SELL(10, TOOL_ID(MAT_DIAMOND, TK_SWORD), 1, 3, 30, 5), SELL(6, TOOL_ID(MAT_GOLD, TK_SWORD), 1, 6, 30, 5),
    },
    [PROF_BUTCHER] = {
        BUY(I_RAW_CHICKEN, 14, 1, 16, 2, 1), BUY(I_RAW_PORK, 7, 1, 16, 2, 1),
        BUY(I_RAW_BEEF, 10, 1, 16, 10, 2), SELL(1, I_COOKED_CHICKEN, 6, 16, 5, 2),
        BUY(I_COAL, 15, 1, 16, 10, 3), SELL(1, I_COOKED_PORK, 5, 16, 10, 3),
        SELL(1, I_STEAK, 4, 12, 15, 4), SELL(1, I_LEATHER, 3, 12, 15, 4),
        SELL(3, I_STEAK, 12, 12, 30, 5), SELL(2, I_COOKED_PORK, 8, 12, 30, 5),
    },
    [PROF_SHEPHERD] = {
        BUY(B_WOOL_WHITE, 18, 1, 16, 2, 1), SELL(2, I_SHEARS, 1, 12, 1, 1),
        BUY(B_FLOWER_YELLOW, 12, 1, 16, 10, 2), SELL(1, B_WOOL_WHITE + 11, 2, 16, 5, 2),
        BUY(B_FLOWER_RED, 12, 1, 16, 20, 3), SELL(1, B_WOOL_WHITE + 10, 2, 16, 10, 3),
        SELL(3, I_BED, 1, 12, 15, 4), SELL(1, B_WOOL_WHITE + 3, 2, 16, 15, 4),
        SELL(1, B_WOOL_WHITE + 6, 3, 16, 30, 5), SELL(1, B_WOOL_WHITE + 5, 3, 16, 30, 5),
    },
    [PROF_FLETCHER] = {
        BUY(I_STICK, 32, 1, 16, 2, 1), SELL(1, I_ARROW, 16, 12, 1, 1),
        TWO(B_GRAVEL, 10, EM, 1, I_FLINT, 10, 12, 5, 2), BUY(I_FLINT, 26, 1, 12, 10, 2),
        BUY(I_STRING, 14, 1, 16, 20, 3), SELL(2, I_BOW, 1, 12, 10, 3),
        BUY(I_FEATHER, 24, 1, 12, 30, 4), SELL(2, I_ARROW, 40, 12, 15, 4),
        SELL(1, I_STRING, 6, 12, 30, 5), SELL(1, I_FEATHER, 8, 12, 30, 5),
    },
    [PROF_MASON] = {
        BUY(I_CLAY_BALL, 10, 1, 16, 2, 1), SELL(1, I_BRICK, 10, 16, 1, 1),
        BUY(B_STONE, 20, 1, 16, 10, 2), SELL(1, B_STONEBRICKS, 4, 16, 5, 2),
        BUY(B_SANDSTONE, 16, 1, 16, 20, 3), SELL(1, B_MOSSY_COBBLESTONE, 4, 16, 10, 3),
        SELL(1, B_BRICKS, 4, 16, 15, 4), SELL(1, B_SANDSTONE, 8, 16, 15, 4),
        SELL(2, B_ICE, 4, 12, 30, 5), SELL(4, B_OBSIDIAN, 2, 12, 30, 5),
    },
    [PROF_CLERIC] = {
        BUY(I_ROTTEN_FLESH, 32, 1, 16, 2, 1), SELL(1, I_REDSTONE, 2, 12, 1, 1),
        BUY(I_GOLD_INGOT, 3, 1, 12, 10, 2), SELL(1, I_GUNPOWDER, 2, 12, 5, 2),
        BUY(I_BONE, 16, 1, 12, 20, 3), SELL(4, B_TNT, 1, 12, 10, 3),
        BUY(B_MUSHROOM_RED, 8, 1, 12, 30, 4), SELL(5, I_LAVA_BUCKET, 1, 4, 15, 4),
        SELL(12, I_DIAMOND, 1, 4, 30, 5), SELL(1, I_REDSTONE, 8, 12, 30, 5),
    },
};

static const char *const names[] = {
    "Ada", "Bram", "Cora", "Dale", "Edda", "Finn", "Gus", "Hilda", "Ivo", "Juna", "Kip", "Lars",
    "Mira", "Ned", "Olga", "Pim", "Quin", "Rosa", "Sven", "Tilda", "Ugo", "Vera", "Wim", "Yuri",
    "Zola", "Abel", "Bea", "Cass", "Dora", "Eli", "Fay", "Gil", "Hal", "Iris", "Jem", "Kaya",
    "Lou", "Moss", "Nell", "Otto", "Pia", "Rex", "Sol", "Tess", "Uma", "Wren", "Xan", "Zed",
};
#define NNAMES ((int)(sizeof(names) / sizeof(names[0])))

static const uint16_t level_xp[VILLAGER_MAX_LEVEL + 1] = { 0, 10, 70, 150, 250, 0xFFFF };
static const char *const level_names[VILLAGER_MAX_LEVEL + 1] = { "", "Novice", "Apprentice", "Journeyman", "Expert", "Master" };

const char *villager_level_name(int level)
{
    return level_names[iclamp(level, 1, VILLAGER_MAX_LEVEL)];
}

int villager_xp_for_level(int level)
{
    return level_xp[iclamp(level, 0, VILLAGER_MAX_LEVEL)];
}

const char *villager_name(const VillagerRec *r)
{
    static char buf[40];
    snprintf(buf, sizeof(buf), "%s the %s", names[r->name % NNAMES], prof_info[r->prof % PROF_COUNT].name);
    return buf;
}

VillagerRec *village_rec(int vil, int slot)
{
    if (vil < 0 || vil >= MAX_VILLAGES || slot < 0 || slot >= VILLAGE_MAX_VILLAGERS || !villages[vil].used)
        return NULL;
    return &villages[vil].v[slot];
}

Village *village_get(int vil)
{
    if (vil < 0 || vil >= MAX_VILLAGES || !villages[vil].used)
        return NULL;
    return &villages[vil];
}

const Trade *villager_trade_def(const VillagerRec *r, int t)
{
    return &trades[r->prof % PROF_COUNT][t];
}

int villager_offers(const VillagerRec *r, uint8_t *idx)
{
    int n = 0;
    for (int t = 0; t < TRADES_PER_PROF; t++) {
        const Trade *d = villager_trade_def(r, t);
        if (d->sell && d->level <= r->level)
            idx[n++] = t;
    }
    return n;
}

int villager_trade_times(const VillagerRec *r, int t)
{
    const Trade *d = villager_trade_def(r, t);
    int n = d->max_uses - r->uses[t];
    if (n <= 0)
        return 0;
    n = imin(n, inv_count_item(player.inv, INV_SIZE, d->buy) / d->buy_n);
    if (d->buy2) {
        int have = inv_count_item(player.inv, INV_SIZE, d->buy2);
        // both inputs may be the same item (emeralds)
        if (d->buy2 == d->buy)
            n = imin(n, have / (d->buy_n + d->buy2_n));
        else
            n = imin(n, have / d->buy2_n);
    }
    return imax(n, 0);
}

int villager_do_trade(VillagerRec *r, int t, int times)
{
    const Trade *d = villager_trade_def(r, t);
    int done = 0;
    int old_level = r->level;
    while (done < times && villager_trade_times(r, t) > 0) {
        inv_remove_item(player.inv, INV_SIZE, d->buy, d->buy_n);
        if (d->buy2)
            inv_remove_item(player.inv, INV_SIZE, d->buy2, d->buy2_n);
        int left = inv_add(player.inv, INV_SIZE, d->sell, d->sell_n, 0);
        if (left > 0) {
            ItemStack s = { .id = d->sell, .count = left };
            entity_spawn_item(player.body.x, player.body.y + FX(1), player.body.z, &s);
        }
        r->uses[t]++;
        r->xp += d->xp;
        player_add_xp(3 + rng_range(4));
        done++;
        while (r->level < VILLAGER_MAX_LEVEL && r->xp >= level_xp[r->level])
            r->level++;
    }
    if (r->level > old_level) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%s is now %s %s!", names[r->name % NNAMES],
                 r->level == 2 || r->level == 4 ? "an" : "a", level_names[r->level]);
        game_message(buf, 90);
        game_message("New trades unlocked", 90);
    }
    return done;
}

// ---------------------------------------------------------------------------
// Doors
// ---------------------------------------------------------------------------

bool village_door_is_open(int x, int y, int z)
{
    int id = world_get(x, y, z);
    return is_door(id) && ((id - B_DOOR) & 4);
}

void village_door_set(int x, int y, int z, bool open)
{
    int id = world_get(x, y, z);
    if (!is_door(id))
        return;
    int by = ((id - B_DOOR) & 8) ? y - 1 : y;
    bool changed = false;
    for (int k = 0; k < 2; k++) {
        int cur = world_get(x, by + k, z);
        if (!is_door(cur))
            continue;
        int idx = cur - B_DOOR;
        int want = open ? (idx | 4) : (idx & ~4);
        if (want != idx) {
            world_set(x, by + k, z, B_DOOR + want);
            changed = true;
        }
    }
    if (changed)
        sound_play_at(SFX_DOOR, (x << 12) + 2048, by << 12, (z << 12) + 2048, 90, 100);
}

// ---------------------------------------------------------------------------
// Generation
// ---------------------------------------------------------------------------

static uint32_t vseed;
static uint32_t vrng(void)
{
    vseed ^= vseed << 13;
    vseed ^= vseed >> 17;
    vseed ^= vseed << 5;
    return vseed;
}
static int vrange(int n) { return (int)((vrng() >> 4) % (uint32_t)n); }

enum { VS_PLAINS = 0, VS_DESERT, VS_SNOWY, VS_FOREST };

typedef struct {
    uint8_t floor, wall, corner, roof, foundation, path, ground, filler;
} Style;

static const Style styles[4] = {
    [VS_PLAINS] = { B_PLANKS_OAK, B_PLANKS_OAK, B_LOG_OAK, B_PLANKS_SPRUCE, B_COBBLESTONE, B_PATH, B_GRASS, B_DIRT },
    [VS_DESERT] = { B_SANDSTONE, B_SANDSTONE, B_SANDSTONE, B_SANDSTONE, B_SANDSTONE, B_SANDSTONE, B_SAND, B_SANDSTONE },
    [VS_SNOWY] = { B_PLANKS_SPRUCE, B_PLANKS_SPRUCE, B_LOG_SPRUCE, B_PLANKS_OAK, B_COBBLESTONE, B_PATH, B_SNOWY_GRASS, B_DIRT },
    [VS_FOREST] = { B_PLANKS_BIRCH, B_PLANKS_BIRCH, B_LOG_BIRCH, B_PLANKS_OAK, B_MOSSY_COBBLESTONE, B_PATH, B_GRASS, B_DIRT },
};
static const Style *st;

static const int8_t fdx[4] = { 0, 1, 0, -1 }, fdz[4] = { -1, 0, 1, 0 };

// Layout grid around the village center
#define G 72
static uint8_t occ[G * G];         // 0 free, 1 road, 2 building
static int gcx, gcz;               // village center (world, unwrapped)

static uint8_t *occ_at(int x, int z)
{
    int lx = x - gcx + G / 2, lz = z - gcz + G / 2;
    if (lx < 0 || lz < 0 || lx >= G || lz >= G)
        return NULL;
    return &occ[lz * G + lx];
}

// Top solid block of a column, or -1 when the surface is water
static int ground_y(int x, int z)
{
    int y = WORLD_H - 2;
    while (y > 0 && !block_solid(world_get(x, y, z)) && !block_liquid(world_get(x, y, z)))
        y--;
    if (block_liquid(world_get(x, y, z)))
        return -1;
    return y;
}

static int site_style(int x, int z)
{
    switch (worldgen_biome(x, z)) {
        case BIOME_PLAINS: return VS_PLAINS;
        case BIOME_DESERT: return VS_DESERT;
        case BIOME_SNOWY: return VS_SNOWY;
        case BIOME_FOREST: return VS_FOREST;
        default: return -1;
    }
}

static bool site_ok(int cx, int cz, int *out_y)
{
    int lo = 99, hi = -1, wet = 0;
    for (int dz = -24; dz <= 24; dz += 4)
        for (int dx = -24; dx <= 24; dx += 4) {
            int y = ground_y(cx + dx, cz + dz);
            if (y < 0) {
                // a little water at the edges is fine (roads bridge it)
                if (++wet > 24 || (iabs(dx) <= 8 && iabs(dz) <= 8))
                    return false;
                continue;
            }
            lo = imin(lo, y);
            hi = imax(hi, y);
        }
    if (hi - lo > 6 || hi + 12 >= WORLD_H)
        return false;
    int y = ground_y(cx, cz);
    if (y < SEA_LEVEL - 1)
        return false;
    *out_y = y;
    return true;
}

// A rectangular plot facing f (the door side), anchored at its front-left corner.
// Local lx runs to the right (looking out of the door), lz from the front wall
// (0) to the back wall (d-1).
typedef struct {
    int ax, az, f, w, d, F;
} Plot;

static void plot_xz(const Plot *p, int lx, int lz, int *x, int *z)
{
    int ox = fdx[p->f], oz = fdz[p->f];
    int rx = -oz, rz = ox;
    *x = p->ax + lx * rx - lz * ox;
    *z = p->az + lx * rz - lz * oz;
}

static void pset(const Plot *p, int lx, int y, int lz, int id)
{
    int x, z;
    plot_xz(p, lx, lz, &x, &z);
    world_set_raw(x, y, z, id);
}

// Directions relative to the plot
static inline int dir_out(const Plot *p) { return p->f; }
static inline int dir_in(const Plot *p) { return (p->f + 2) & 3; }
static inline int dir_right(const Plot *p) { return (p->f + 1) & 3; }
static inline int dir_left(const Plot *p) { return (p->f + 3) & 3; }

static bool plot_free(const Plot *p, int margin)
{
    for (int lz = -margin; lz < p->d + margin; lz++)
        for (int lx = -margin; lx < p->w + margin; lx++) {
            int x, z;
            plot_xz(p, lx, lz, &x, &z);
            uint8_t *o = occ_at(x, z);
            if (!o || *o)
                return false;
        }
    // ground roughly level and dry under the footprint
    static const int8_t cl[5][2] = { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 }, { 2, 2 } };
    for (int i = 0; i < 5; i++) {
        int lx = cl[i][0] == 2 ? p->w / 2 : cl[i][0] * (p->w - 1);
        int lz = cl[i][1] == 2 ? p->d / 2 : cl[i][1] * (p->d - 1);
        int x, z;
        plot_xz(p, lx, lz, &x, &z);
        int y = ground_y(x, z);
        if (y < 0 || iabs(y - p->F) > 3)
            return false;
    }
    return true;
}

static void plot_mark(const Plot *p, int margin, uint8_t v)
{
    for (int lz = -margin; lz < p->d + margin; lz++)
        for (int lx = -margin; lx < p->w + margin; lx++) {
            int x, z;
            plot_xz(p, lx, lz, &x, &z);
            uint8_t *o = occ_at(x, z);
            if (o)
                *o = v;
        }
}

// Flatten the plot (and a margin around it) to floor height F
static void level_plot(const Plot *p, int margin, int top_inside)
{
    for (int lz = -margin; lz < p->d + margin; lz++)
        for (int lx = -margin; lx < p->w + margin; lx++) {
            int x, z;
            plot_xz(p, lx, lz, &x, &z);
            bool inside = lx >= 0 && lz >= 0 && lx < p->w && lz < p->d;
            for (int y = p->F + 1; y < WORLD_H && y <= p->F + 14; y++)
                world_set_raw(x, y, z, B_AIR);
            world_set_raw(x, p->F, z, inside ? top_inside : st->ground);
            for (int y = p->F - 1; y > 0 && y > p->F - 10; y--) {
                int id = world_get(x, y, z);
                if (block_solid(id) && id != B_SAND)
                    break;
                world_set_raw(x, y, z, inside ? st->foundation : st->filler);
            }
        }
}

static void build_walls(const Plot *p, int h, int wall, int corner)
{
    for (int lz = 0; lz < p->d; lz++)
        for (int lx = 0; lx < p->w; lx++) {
            bool ex = lx == 0 || lx == p->w - 1, ez = lz == 0 || lz == p->d - 1;
            if (!ex && !ez)
                continue;
            for (int y = p->F + 1; y <= p->F + h; y++)
                pset(p, lx, y, lz, ex && ez ? corner : wall);
        }
}

// Stepped gable roof with eaves over the front and back walls
static void build_roof(const Plot *p, int y0)
{
    for (int k = 0; ; k++) {
        int z0 = -1 + k, z1 = p->d - k;
        if (z0 > z1 || y0 + k >= WORLD_H)
            break;
        for (int lz = z0; lz <= z1; lz++)
            for (int lx = 0; lx < p->w; lx++) {
                bool edge = (lz == z0 || lz == z1) && k == 0;
                pset(p, lx, y0 + k, lz, edge ? st->corner : st->roof);
            }
    }
}

static void place_door(const Plot *p, int lx)
{
    int df = dir_in(p);   // closed panel on the outer edge
    pset(p, lx, p->F + 1, 0, B_DOOR + df);
    pset(p, lx, p->F + 2, 0, B_DOOR + df + 8);
}

static void place_windows(const Plot *p, int y)
{
    if (p->d >= 4) {
        pset(p, 0, y, p->d / 2, B_GLASS);
        pset(p, p->w - 1, y, p->d / 2, B_GLASS);
    }
    if (p->w >= 5)
        pset(p, p->w / 2, y, p->d - 1, B_GLASS);
    if (p->w >= 7) {
        pset(p, 1, y, 0, B_GLASS);
        pset(p, p->w - 2, y, 0, B_GLASS);
    }
}

static void fill_chest(Chest *c, int kind);

static void place_chest(const Plot *p, int lx, int lz, int facing, int loot)
{
    int x, z;
    plot_xz(p, lx, lz, &x, &z);
    world_set_raw(x, p->F + 1, z, B_CHEST + facing);
    Chest *c = chest_get(x, p->F + 1, z, true);
    if (c)
        fill_chest(c, loot);
}

enum { LOOT_HOUSE = 0, LOOT_SMITH, LOOT_FLETCHER, LOOT_BUTCHER, LOOT_MASON };

static void loot_add(Chest *c, int id, int lo, int hi)
{
    int n = lo + vrange(hi - lo + 1);
    if (n <= 0)
        return;
    for (int tries = 0; tries < 8; tries++) {
        ItemStack *s = &c->items[vrange(CHEST_SIZE)];
        if (stack_empty(s)) {
            s->id = id;
            s->count = n;
            s->damage = 0;
            return;
        }
    }
}

static void fill_chest(Chest *c, int kind)
{
    switch (kind) {
        case LOOT_SMITH:
            loot_add(c, I_IRON_INGOT, 1, 5);
            loot_add(c, I_BREAD, 1, 3);
            loot_add(c, I_APPLE, 0, 3);
            loot_add(c, B_OBSIDIAN, 0, 4);
            if (vrange(3) == 0) loot_add(c, TOOL_ID(MAT_IRON, TK_PICKAXE), 1, 1);
            if (vrange(3) == 0) loot_add(c, TOOL_ID(MAT_IRON, TK_SWORD), 1, 1);
            if (vrange(4) == 0) loot_add(c, ARMOR_ID(AM_IRON, AS_HEAD), 1, 1);
            if (vrange(5) == 0) loot_add(c, I_DIAMOND, 1, 2);
            loot_add(c, I_EMERALD, 0, 2);
            break;
        case LOOT_FLETCHER:
            loot_add(c, I_ARROW, 4, 12);
            loot_add(c, I_FLINT, 1, 4);
            loot_add(c, I_STICK, 2, 8);
            loot_add(c, I_FEATHER, 0, 3);
            break;
        case LOOT_BUTCHER:
            loot_add(c, I_RAW_PORK, 1, 3);
            loot_add(c, I_RAW_BEEF, 0, 3);
            loot_add(c, I_COAL, 1, 4);
            break;
        case LOOT_MASON:
            loot_add(c, I_CLAY_BALL, 2, 6);
            loot_add(c, B_STONEBRICKS, 2, 8);
            loot_add(c, I_BRICK, 0, 4);
            break;
        default:
            loot_add(c, I_BREAD, 1, 4);
            loot_add(c, I_WHEAT, 0, 6);
            loot_add(c, I_SEEDS, 1, 6);
            loot_add(c, I_APPLE, 0, 2);
            if (vrange(2) == 0) loot_add(c, I_EMERALD, 1, 2);
            break;
    }
}

// Building kinds
enum {
    K_HOUSE_S = 0, K_HOUSE_L, K_SMITHY, K_LIBRARY, K_BUTCHER, K_SHEPHERD, K_FLETCHER, K_MASON, K_CHURCH, K_FARM,
    K_COUNT
};
static const uint8_t kind_w[K_COUNT] = { 5, 7, 7, 7, 5, 5, 5, 5, 5, 9 };
static const uint8_t kind_d[K_COUNT] = { 5, 6, 7, 6, 6, 6, 6, 6, 5, 7 };

static Village *gv;     // village being generated

static VillagerRec *add_villager(const Plot *p, int prof, int bed_lx, int wx, int wy, int wz)
{
    if (gv->nvillagers >= VILLAGE_MAX_VILLAGERS)
        return NULL;
    VillagerRec *r = &gv->v[gv->nvillagers++];
    memset(r, 0, sizeof(*r));
    r->alive = 1;
    r->prof = prof;
    r->level = vrange(5) == 0 ? 2 : 1;
    r->xp = level_xp[r->level - 1];
    r->name = vrange(NNAMES);
    r->health = 20;
    int x, z;
    plot_xz(p, bed_lx, p->d - 2, &x, &z);
    r->bed_x = x & world.mask; r->bed_z = z & world.mask; r->bed_y = p->F + 1;
    plot_xz(p, p->w / 2, p->d - 3, &x, &z);
    r->stand_x = x & world.mask; r->stand_z = z & world.mask; r->stand_y = p->F + 1;
    r->x = r->stand_x; r->z = r->stand_z; r->y = r->stand_y;
    plot_xz(p, p->w / 2, 0, &x, &z);
    r->door_x = x & world.mask; r->door_z = z & world.mask; r->door_y = p->F + 1;
    r->door_f = dir_out(p);
    r->work_x = wx & world.mask; r->work_y = wy; r->work_z = wz & world.mask;
    return r;
}

// Bed against the back wall at lx with its head toward the back
static void place_bed(const Plot *p, int lx)
{
    int bf = dir_in(p);
    pset(p, lx, p->F + 1, p->d - 3, B_BED + bf);
    pset(p, lx, p->F + 1, p->d - 2, B_BED + bf + 4);
}

static void house_basics(const Plot *p, int h, int wall, int floor)
{
    level_plot(p, 1, floor);
    build_walls(p, h, wall, st->corner);
    build_roof(p, p->F + h + 1);
    place_door(p, p->w / 2);
    place_windows(p, p->F + 2);
    // light inside so monsters never spawn there
    pset(p, p->w / 2, p->F + 3, p->d - 2, B_TORCH_N + dir_out(p));
}

static int random_home_prof(void)
{
    static const uint8_t home[] = { PROF_FARMER, PROF_FARMER, PROF_SHEPHERD, PROF_FLETCHER, PROF_BUTCHER, PROF_MASON };
    return home[vrange(sizeof(home))];
}

static void build(const Plot *p, int kind)
{
    int x, z;
    switch (kind) {
        case K_FARM: {
            level_plot(p, 1, st->corner);
            for (int lz = 0; lz < p->d; lz++)
                for (int lx = 0; lx < p->w; lx++) {
                    bool border = lx == 0 || lz == 0 || lx == p->w - 1 || lz == p->d - 1;
                    if (border)
                        pset(p, lx, p->F, lz, st->corner);
                    else if (lx == p->w / 2)
                        pset(p, lx, p->F, lz, B_WATER);
                    else {
                        pset(p, lx, p->F, lz, B_FARMLAND_WET);
                        pset(p, lx, p->F + 1, lz, B_WHEAT + vrange(8));
                    }
                }
            plot_xz(p, p->w / 2, p->d / 2, &x, &z);
            gv->farm_x = x & world.mask; gv->farm_z = z & world.mask; gv->farm_y = p->F + 1;
            return;
        }
        case K_CHURCH: {
            level_plot(p, 1, st->foundation);
            build_walls(p, 8, st->foundation, st->corner);
            for (int k = 0; k < 3; k++)
                for (int lz = k; lz < p->d - k; lz++)
                    for (int lx = k; lx < p->w - k; lx++)
                        pset(p, lx, p->F + 9 + k, lz, k == 2 ? st->corner : st->foundation);
            pset(p, p->w / 2, p->F + 12, p->d / 2, B_TORCH);
            place_door(p, p->w / 2);
            place_windows(p, p->F + 2);
            place_windows(p, p->F + 6);
            pset(p, p->w / 2, p->F + 3, p->d - 2, B_TORCH_N + dir_out(p));
            pset(p, p->w / 2, p->F + 6, p->d - 2, B_TORCH_N + dir_out(p));
            place_bed(p, p->w / 2 - 1);
            pset(p, p->w - 2, p->F + 1, p->d - 2, B_BOOKSHELF);
            plot_xz(p, p->w - 2, p->d - 3, &x, &z);
            add_villager(p, PROF_CLERIC, p->w / 2 - 1, x, p->F + 1, z);
            return;
        }
        case K_SMITHY: {
            house_basics(p, 4, st->foundation, st->foundation);
            int fl = dir_left(p);
            pset(p, p->w - 2, p->F + 1, p->d - 2, B_FURNACE + fl);
            pset(p, p->w - 2, p->F + 1, p->d - 3, B_FURNACE + fl);
            pset(p, p->w - 2, p->F + 1, 1, B_CRAFTING_TABLE);
            place_chest(p, 1, 1, dir_right(p), LOOT_SMITH);
            place_bed(p, p->w / 2 - 1);
            static const uint8_t smiths[3] = { PROF_ARMORER, PROF_TOOLSMITH, PROF_WEAPONSMITH };
            plot_xz(p, p->w - 3, p->d - 2, &x, &z);
            add_villager(p, smiths[vrange(3)], p->w / 2 - 1, x, p->F + 1, z);
            return;
        }
        case K_LIBRARY: {
            house_basics(p, 4, st->wall, st->floor);
            for (int lz = 1; lz < p->d - 1; lz++)
                for (int y = p->F + 1; y <= p->F + 2; y++) {
                    pset(p, 1, y, lz, B_BOOKSHELF);
                    pset(p, p->w - 2, y, lz, B_BOOKSHELF);
                }
            place_bed(p, p->w / 2 - 1);
            plot_xz(p, p->w - 3, p->d - 2, &x, &z);
            add_villager(p, PROF_LIBRARIAN, p->w / 2 - 1, x, p->F + 1, z);
            return;
        }
        case K_HOUSE_L: {
            house_basics(p, 3, st->wall, st->floor);
            place_bed(p, p->w / 2 - 1);
            place_bed(p, p->w / 2 + 1);
            pset(p, p->w - 2, p->F + 1, 1, B_CRAFTING_TABLE);
            if (vrange(2))
                place_chest(p, 1, 1, dir_right(p), LOOT_HOUSE);
            int a = random_home_prof(), b = random_home_prof();
            plot_xz(p, p->w / 2, p->d - 3, &x, &z);
            add_villager(p, a, p->w / 2 - 1, x, p->F + 1, z);
            add_villager(p, b, p->w / 2 + 1, x, p->F + 1, z);
            return;
        }
        default: {
            // small houses and the craft shops share one plan with a job block
            int wall = kind == K_MASON ? B_STONEBRICKS : st->wall;
            house_basics(p, 3, wall, st->floor);
            place_bed(p, p->w / 2 - 1);
            int job = B_CRAFTING_TABLE, loot = -1, prof = random_home_prof();
            switch (kind) {
                case K_BUTCHER: job = B_FURNACE + dir_left(p); loot = LOOT_BUTCHER; prof = PROF_BUTCHER; break;
                case K_SHEPHERD:
                    job = B_WOOL_WHITE; prof = PROF_SHEPHERD;
                    for (int lz = 1; lz < p->d - 1; lz++)
                        for (int lx = 1; lx < p->w - 1; lx++)
                            pset(p, lx, p->F, lz, B_WOOL_WHITE + vrange(16));
                    break;
                case K_FLETCHER: loot = LOOT_FLETCHER; prof = PROF_FLETCHER; break;
                case K_MASON: job = B_FURNACE + dir_left(p); loot = LOOT_MASON; prof = PROF_MASON; break;
                default: if (vrange(3) == 0) loot = LOOT_HOUSE; break;
            }
            pset(p, p->w - 2, p->F + 1, p->d - 2, job);
            if (loot >= 0 && p->d >= 6)
                place_chest(p, p->w - 2, 1, dir_left(p), loot);
            plot_xz(p, p->w - 2, p->d - 3, &x, &z);
            add_villager(p, prof, p->w / 2 - 1, x, p->F + 1, z);
            return;
        }
    }
}

typedef struct {
    int sx, sz, dir, len;
} Road;

static void mark_road(const Road *r, int pass)
{
    int px = -fdz[r->dir], pz = fdx[r->dir];   // perpendicular
    for (int t = 0; t < r->len; t++)
        for (int k = -1; k <= 1; k++) {
            int x = r->sx + fdx[r->dir] * t + px * k, z = r->sz + fdz[r->dir] * t + pz * k;
            uint8_t *o = occ_at(x, z);
            if (!o)
                continue;
            if (pass == 0) {
                if (!*o)
                    *o = 1;
            } else if (*o == 1) {
                int y = ground_y(x, z);
                if (y < 0) {
                    // bridge over water
                    world_set_raw(x, SEA_LEVEL, z, st->floor);
                    continue;
                }
                int id = world_get(x, y, z);
                if (id == B_GRASS || id == B_DIRT || id == B_SAND || id == B_SNOWY_GRASS || id == B_GRAVEL || id == B_STONE)
                    world_set_raw(x, y, z, st->path);
                for (int yy = y + 1; yy < y + 3 && yy < WORLD_H; yy++)
                    if (!block_solid(world_get(x, yy, z)))
                        world_set_raw(x, yy, z, B_AIR);
            }
        }
}

static void build_well(int cx, int cz, int F)
{
    // plaza
    for (int dz = -4; dz <= 3; dz++)
        for (int dx = -4; dx <= 3; dx++) {
            int x = cx + dx, z = cz + dz;
            for (int y = F + 1; y < WORLD_H && y <= F + 10; y++)
                world_set_raw(x, y, z, B_AIR);
            world_set_raw(x, F, z, st->path);
            for (int y = F - 1; y > 0 && y > F - 8; y--) {
                if (block_solid(world_get(x, y, z)) && world_get(x, y, z) != B_SAND)
                    break;
                world_set_raw(x, y, z, st->filler);
            }
        }
    // well shaft: 4x4 with 2x2 water
    for (int dz = -2; dz <= 1; dz++)
        for (int dx = -2; dx <= 1; dx++) {
            int x = cx + dx, z = cz + dz;
            bool inner = dx >= -1 && dx <= 0 && dz >= -1 && dz <= 0;
            world_set_raw(x, F - 4, z, st->foundation);
            for (int y = F - 3; y <= F; y++)
                world_set_raw(x, y, z, inner ? B_WATER : st->foundation);
            if (!inner)
                world_set_raw(x, F + 1, z, st->foundation);
            bool corner = (dx == -2 || dx == 1) && (dz == -2 || dz == 1);
            if (corner) {
                world_set_raw(x, F + 2, z, st->corner);
                world_set_raw(x, F + 3, z, st->corner);
            }
            world_set_raw(x, F + 4, z, st->roof);
        }
    world_set_raw(cx, F + 5, cz, st->roof);
    world_set_raw(cx - 1, F + 5, cz - 1, st->roof);
    world_set_raw(cx, F + 6, cz - 1, B_TORCH);
}

static void place_lamp(int x, int z)
{
    int y = ground_y(x, z);
    if (y < 0 || y + 4 >= WORLD_H)
        return;
    world_set_raw(x, y + 1, z, st->foundation);
    world_set_raw(x, y + 2, z, st->corner);
    world_set_raw(x, y + 3, z, B_TORCH);
}

static bool make_village(int cx, int cz, int F, int style)
{
    gv = NULL;
    for (int i = 0; i < MAX_VILLAGES; i++)
        if (!villages[i].used) {
            gv = &villages[i];
            break;
        }
    if (!gv)
        return false;
    memset(gv, 0, sizeof(*gv));
    st = &styles[style];
    gcx = cx;
    gcz = cz;
    memset(occ, 0, sizeof(occ));
    for (int dz = -4; dz <= 3; dz++)
        for (int dx = -4; dx <= 3; dx++)
            *occ_at(cx + dx, cz + dz) = 2;

    // Roads out of the plaza, with an occasional side street
    Road roads[8];
    int nroads = 0;
    for (int d = 0; d < 4; d++) {
        Road *r = &roads[nroads++];
        r->dir = d;
        r->sx = cx + fdx[d] * 5 - (d == 1 ? 1 : 0);
        r->sz = cz + fdz[d] * 5 - (d == 2 ? 1 : 0);
        r->len = 12 + vrange(12);
        if (r->len >= 16 && vrange(2)) {
            Road *b = &roads[nroads++];
            int side = vrange(2) ? (d + 1) & 3 : (d + 3) & 3;
            int t = 6 + vrange(r->len - 10);
            b->dir = side;
            b->sx = r->sx + fdx[d] * t + fdx[side] * 2;
            b->sz = r->sz + fdz[d] * t + fdz[side] * 2;
            b->len = 8 + vrange(6);
        }
    }
    for (int i = 0; i < nroads; i++)
        mark_road(&roads[i], 0);

    // Buildings to place, most important first
    uint8_t want[16];
    int nwant = 0;
    want[nwant++] = K_FARM;
    want[nwant++] = K_SMITHY;
    want[nwant++] = K_HOUSE_L;
    uint8_t extra[] = { K_LIBRARY, K_CHURCH, K_BUTCHER, K_SHEPHERD, K_FLETCHER, K_MASON, K_HOUSE_S, K_HOUSE_S };
    for (int i = (int)sizeof(extra) - 1; i > 0; i--) {
        int j = vrange(i + 1);
        uint8_t t = extra[i]; extra[i] = extra[j]; extra[j] = t;
    }
    for (unsigned i = 0; i < sizeof(extra); i++)
        want[nwant++] = extra[i];
    if (vrange(2))
        want[nwant++] = K_FARM;
    int next = 0;

    // Walk along each road and try plots on both sides
    for (int i = 0; i < nroads && next < nwant; i++) {
        Road *r = &roads[i];
        int px = -fdz[r->dir], pz = fdx[r->dir];
        for (int side = -1; side <= 1; side += 2) {
            int t = 2;
            while (t < r->len - 1 && next < nwant) {
                if (gv->nvillagers >= VILLAGE_MAX_VILLAGERS && want[next] != K_FARM) {
                    next++;
                    continue;
                }
                int kind = want[next];
                int w = kind_w[kind], d = kind_d[kind];
                // front wall center three cells off the road center line
                int fx = r->sx + fdx[r->dir] * (t + w / 2) + px * side * 3;
                int fz = r->sz + fdz[r->dir] * (t + w / 2) + pz * side * 3;
                Plot p;
                // facing the road: outward direction is toward the road
                int sdir = side > 0 ? (r->dir + 1) & 3 : (r->dir + 3) & 3;   // direction from road to plot
                p.f = (sdir + 2) & 3;
                p.w = w;
                p.d = d;
                int rx = -fdz[p.f], rz = fdx[p.f];
                p.ax = fx - rx * (w / 2);
                p.az = fz - rz * (w / 2);
                int gy = ground_y(fx, fz);
                p.F = gy;
                if (gy >= SEA_LEVEL - 1 && plot_free(&p, 1)) {
                    plot_mark(&p, 1, 2);
                    build(&p, kind);
                    // a short path from the door to the road
                    if (kind != K_FARM) {
                        for (int k = 1; k <= 2; k++) {
                            int x, z;
                            plot_xz(&p, w / 2, -k, &x, &z);
                            uint8_t *o = occ_at(x, z);
                            if (o)
                                *o = 1;
                        }
                    }
                    next++;
                    t += w + 1;
                } else {
                    t += 2;
                }
            }
        }
    }
    if (gv->nvillagers < 2) {
        // too cramped to be a village: keep the buildings, forget the record
        gv->used = 0;
        return false;
    }

    build_well(cx, cz, F);
    for (int i = 0; i < nroads; i++)
        mark_road(&roads[i], 1);
    // door paths
    for (int lz = 0; lz < G; lz++)
        for (int lx = 0; lx < G; lx++)
            if (occ[lz * G + lx] == 1) {
                int x = gcx + lx - G / 2, z = gcz + lz - G / 2;
                int y = ground_y(x, z);
                if (y > 0) {
                    int id = world_get(x, y, z);
                    if (id == B_GRASS || id == B_DIRT || id == B_SAND || id == B_SNOWY_GRASS)
                        world_set_raw(x, y, z, st->path);
                }
            }
    // street lamps beside the roads
    for (int i = 0; i < nroads; i++) {
        Road *r = &roads[i];
        int px = -fdz[r->dir], pz = fdx[r->dir];
        for (int t = 4; t < r->len; t += 9) {
            int side = (t / 9) & 1 ? 1 : -1;
            int x = r->sx + fdx[r->dir] * t + px * side * 2, z = r->sz + fdz[r->dir] * t + pz * side * 2;
            uint8_t *o = occ_at(x, z);
            if (o && !*o) {
                *o = 2;
                place_lamp(x, z);
            }
        }
    }

    gv->used = 1;
    gv->style = style;
    gv->x = cx & world.mask;
    gv->z = cz & world.mask;
    gv->y = F + 1;
    if (gv->nvillagers >= 3) {
        gv->golem_alive = 1;
        gv->golem_health = 100;
        gv->golem_x = (cx + 3) & world.mask;
        gv->golem_z = cz & world.mask;
        gv->golem_y = F + 1;
    }
    if (!gv->farm_y) {
        gv->farm_x = gv->x;
        gv->farm_z = gv->z;
        gv->farm_y = gv->y;
    }
    // farmers work at the farm
    for (int i = 0; i < gv->nvillagers; i++)
        if (gv->v[i].prof == PROF_FARMER) {
            gv->v[i].work_x = gv->farm_x;
            gv->v[i].work_z = gv->farm_z;
            gv->v[i].work_y = gv->farm_y;
        }
    return true;
}

void villages_clear(void)
{
    memset(villages, 0, sizeof(villages));
}

int villages_generate(uint32_t seed, uint8_t *mask)
{
    villages_clear();
    vseed = (seed ^ 0x5EED1234u) | 1;
    int ws = world.size;
    int max = ws >= 256 ? 6 : 2;
    int spacing = 64;
    int count = 0;
    // candidate centers on a jittered grid, tried in random order
    int step = 32, n = ws / step;
    int cand[64][2];
    int nc = 0;
    for (int gz = 0; gz < n; gz++)
        for (int gx = 0; gx < n; gx++)
            if (nc < 64) {
                cand[nc][0] = gx * step + 8 + vrange(16);
                cand[nc][1] = gz * step + 8 + vrange(16);
                nc++;
            }
    for (int i = nc - 1; i > 0; i--) {
        int j = vrange(i + 1);
        int a = cand[i][0], b = cand[i][1];
        cand[i][0] = cand[j][0]; cand[i][1] = cand[j][1];
        cand[j][0] = a; cand[j][1] = b;
    }
    for (int i = 0; i < nc && count < max; i++) {
        int cx = cand[i][0], cz = cand[i][1];
        bool near = false;
        for (int k = 0; k < MAX_VILLAGES; k++)
            if (villages[k].used && imax(iabs(world_wrap_delta(cx - villages[k].x)), iabs(world_wrap_delta(cz - villages[k].z))) < spacing)
                near = true;
        if (near)
            continue;
        int style = site_style(cx, cz);
        int F;
        if (style < 0 || !site_ok(cx, cz, &F))
            continue;
        if (make_village(cx, cz, F, style)) {
            count++;
            if (mask) {
                for (int lz = 0; lz < G; lz++)
                    for (int lx = 0; lx < G; lx++) {
                        int x = (gcx + lx - G / 2) & world.mask, z = (gcz + lz - G / 2) & world.mask;
                        // keep trees out of the village, and off the streets nearby
                        bool busy = false;
                        for (int dz = -2; dz <= 2 && !busy; dz++)
                            for (int dx = -2; dx <= 2 && !busy; dx++) {
                                int ax = lx + dx, az = lz + dz;
                                if (ax >= 0 && az >= 0 && ax < G && az < G && occ[az * G + ax])
                                    busy = true;
                            }
                        if (busy)
                            mask[(z << world.shift) + x] = 1;
                    }
            }
        }
    }
    return count;
}

// ---------------------------------------------------------------------------
// Bringing villagers to life near the player
// ---------------------------------------------------------------------------

static int32_t prev_time = -1;

void villages_unloaded(void)
{
    for (int i = 0; i < MAX_VILLAGES; i++) {
        villages[i].golem_loaded = 0;
        for (int k = 0; k < VILLAGE_MAX_VILLAGERS; k++)
            villages[i].v[k].loaded = 0;
    }
    prev_time = -1;
}

static int find_entity(int type, int vil, int slot)
{
    for (int i = 0; i < MAX_ENTITIES; i++) {
        Entity *e = &entities[i];
        if (e->type == type && e->vil == vil && e->vslot == slot)
            return i;
    }
    return -1;
}

// A free spot for a mob at (x, y, z), or nearby
static bool spot_free(int x, int y, int z)
{
    return !block_solid(world_get(x, y, z)) && !block_solid(world_get(x, y + 1, z)) && !block_liquid(world_get(x, y, z));
}

static void entity_from_rec(Entity *e, int x, int y, int z, int alt_x, int alt_y, int alt_z)
{
    if (!spot_free(x, y, z)) {
        x = alt_x; y = alt_y; z = alt_z;
        for (int k = 0; k < 8 && !spot_free(x, y, z); k++)
            y++;
    }
    e->body.x = world_wrap_fx((x << 12) + 2048);
    e->body.y = y << 12;
    e->body.z = world_wrap_fx((z << 12) + 2048);
}

static void load_village(int vi)
{
    Village *v = &villages[vi];
    for (int k = 0; k < v->nvillagers; k++) {
        VillagerRec *r = &v->v[k];
        if (!r->alive || r->loaded)
            continue;
        int i = entity_spawn(ET_VILLAGER, 0, 0, 0);
        if (i < 0)
            return;   // no room right now, try again later
        Entity *e = &entities[i];
        e->vil = vi;
        e->vslot = k;
        e->health = r->health ? r->health : 20;
        entity_from_rec(e, r->x, r->y, r->z, r->stand_x, r->stand_y, r->stand_z);
        r->loaded = 1;
    }
    if (v->golem_alive && !v->golem_loaded) {
        int i = entity_spawn(ET_GOLEM, 0, 0, 0);
        if (i < 0)
            return;
        Entity *e = &entities[i];
        e->vil = vi;
        e->vslot = 0;
        e->health = v->golem_health ? v->golem_health : 100;
        entity_from_rec(e, v->golem_x, v->golem_y, v->golem_z, v->x + 3, v->y, v->z);
        v->golem_loaded = 1;
    }
}

static void store_villager(Entity *e)
{
    VillagerRec *r = village_rec(e->vil, e->vslot);
    if (!r)
        return;
    r->x = (e->body.x >> 12) & world.mask;
    r->y = iclamp(e->body.y >> 12, 1, WORLD_H - 2);
    r->z = (e->body.z >> 12) & world.mask;
    r->health = iclamp(e->health, 1, 20);
}

static void store_golem(Entity *e)
{
    Village *v = village_get(e->vil);
    if (!v)
        return;
    v->golem_x = (e->body.x >> 12) & world.mask;
    v->golem_y = iclamp(e->body.y >> 12, 1, WORLD_H - 2);
    v->golem_z = (e->body.z >> 12) & world.mask;
    v->golem_health = iclamp(e->health, 1, 100);
}

static void unload_village(int vi)
{
    Village *v = &villages[vi];
    for (int k = 0; k < v->nvillagers; k++) {
        VillagerRec *r = &v->v[k];
        if (!r->loaded)
            continue;
        int i = find_entity(ET_VILLAGER, vi, k);
        if (i >= 0) {
            if (entities[i].ai == 9)
                r->alive = 0;     // was dying
            else
                store_villager(&entities[i]);
            entities[i].type = ET_NONE;
        }
        r->loaded = 0;
    }
    if (v->golem_loaded) {
        int i = find_entity(ET_GOLEM, vi, 0);
        if (i >= 0) {
            if (entities[i].ai == 9)
                v->golem_alive = 0;
            else
                store_golem(&entities[i]);
            entities[i].type = ET_NONE;
        }
        v->golem_loaded = 0;
    }
}

void villages_sync(void)
{
    for (int i = 0; i < MAX_ENTITIES; i++) {
        Entity *e = &entities[i];
        if (e->type == ET_VILLAGER && e->ai != 9)
            store_villager(e);
        else if (e->type == ET_GOLEM && e->ai != 9)
            store_golem(e);
    }
}

static void new_day(int vi, bool near)
{
    Village *v = &villages[vi];
    int alive = 0;
    for (int k = 0; k < v->nvillagers; k++)
        alive += v->v[k].alive;
    // an empty bed attracts a new villager now and then
    if (alive < v->nvillagers && vrange(2) == 0) {
        for (int k = 0; k < v->nvillagers; k++) {
            VillagerRec *r = &v->v[k];
            if (r->alive)
                continue;
            r->alive = 1;
            r->loaded = 0;
            r->level = 1;
            r->xp = 0;
            r->name = rng_range(NNAMES);
            r->health = 20;
            memset(r->uses, 0, sizeof(r->uses));
            r->x = r->stand_x; r->y = r->stand_y; r->z = r->stand_z;
            if (near) {
                char buf[48];
                snprintf(buf, sizeof(buf), "%s moved into the village", names[r->name]);
                game_message(buf, 90);
            }
            break;
        }
    }
    if (!v->golem_alive) {
        if (++v->golem_days >= 3 && alive >= 3) {
            v->golem_alive = 1;
            v->golem_days = 0;
            v->golem_health = 100;
            v->golem_x = v->x + 3;
            v->golem_y = v->y;
            v->golem_z = v->z;
        }
    }
}

void villages_tick(void)
{
    int32_t t = world.time;
    if (prev_time >= 0 && t != prev_time) {
        bool dawn = t < prev_time;
        bool restock = dawn || (prev_time < 9000 && t >= 9000);
        int px = player.body.x >> 12, pz = player.body.z >> 12;
        for (int i = 0; i < MAX_VILLAGES; i++) {
            Village *v = &villages[i];
            if (!v->used)
                continue;
            if (restock)
                for (int k = 0; k < v->nvillagers; k++)
                    memset(v->v[k].uses, 0, sizeof(v->v[k].uses));
            if (dawn) {
                int d = imax(iabs(world_wrap_delta(v->x - px)), iabs(world_wrap_delta(v->z - pz)));
                new_day(i, d < 52);
            }
        }
    }
    prev_time = t;
    if (world.ticks % 10)
        return;
    int px = player.body.x >> 12, pz = player.body.z >> 12;
    for (int i = 0; i < MAX_VILLAGES; i++) {
        Village *v = &villages[i];
        if (!v->used)
            continue;
        // loaded villagers whose entity is gone have died (or fell out of the world)
        for (int k = 0; k < v->nvillagers; k++) {
            VillagerRec *r = &v->v[k];
            if (r->loaded && find_entity(ET_VILLAGER, i, k) < 0) {
                r->loaded = 0;
                r->alive = 0;
            }
        }
        if (v->golem_loaded && find_entity(ET_GOLEM, i, 0) < 0) {
            v->golem_loaded = 0;
            v->golem_alive = 0;
            v->golem_days = 0;
        }
        // load within view, unload a little farther out (small worlds wrap at 64)
        int load_r = imin(52, world.size / 2 - 16);
        int d = imax(iabs(world_wrap_delta(v->x - px)), iabs(world_wrap_delta(v->z - pz)));
        if (d < load_r)
            load_village(i);
        else if (d > load_r + 12)
            unload_village(i);
    }
}
