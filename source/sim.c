// Block updates: liquids, gravity, support, growth and random ticks
#include <stdlib.h>
#include "sim.h"
#include "world.h"
#include "blocks.h"
#include "items.h"
#include "entity.h"
#include "inventory.h"
#include "worldgen.h"
#include "player.h"
#include "game.h"
#include "sound.h"

#define MAX_UPDATES 3072

typedef struct {
    int16_t x, z;
    int8_t y;
    uint8_t pad;
    uint16_t due;   // tick (low 16 bits)
} Update;

static Update updates[MAX_UPDATES];
static int nupdates;
static uint16_t sim_now;

void sim_clear(void)
{
    nupdates = 0;
}

void sim_init(void)
{
    sim_clear();
}

void sim_schedule(int x, int y, int z, int delay)
{
    if (y < 0 || y >= WORLD_H)
        return;
    x &= world.mask;
    z &= world.mask;
    uint16_t due = sim_now + (delay > 0 ? delay : 1);
    // avoid exact duplicates among recent entries
    int start = nupdates > 24 ? nupdates - 24 : 0;
    for (int i = start; i < nupdates; i++)
        if (updates[i].x == x && updates[i].y == y && updates[i].z == z)
            return;
    if (nupdates >= MAX_UPDATES)
        return;
    updates[nupdates++] = (Update){ x, z, y, 0, due };
}

static int update_delay(int id)
{
    if (is_water(id))
        return 5;
    if (is_lava(id))
        return 30;
    if (blocks[id].flags & BF_GRAVITY)
        return 2;
    if (id == B_FIRE)
        return 30 + rng_range(30);
    return 1;
}

void sim_notify(int x, int y, int z)
{
    int id = world_get(x, y, z);
    sim_schedule(x, y, z, update_delay(id));
    for (int f = 0; f < 6; f++) {
        int nx = x + face_dx[f], ny = y + face_dy[f], nz = z + face_dz[f];
        int nid = world_get(nx, ny, nz);
        if (nid != B_AIR)
            sim_schedule(nx, ny, nz, update_delay(nid));
    }
}

// ---------------------------------------------------------------------------
// Support rules
// ---------------------------------------------------------------------------

static bool is_soil(int id)
{
    return id == B_GRASS || id == B_DIRT || id == B_FARMLAND || id == B_FARMLAND_WET || id == B_SNOWY_GRASS;
}

bool sim_can_stay(int x, int y, int z, int id)
{
    int below = world_get(x, y - 1, z);
    switch (id) {
        case B_FLOWER_RED: case B_FLOWER_YELLOW: case B_TALLGRASS:
        case B_SAPLING_OAK: case B_SAPLING_BIRCH: case B_SAPLING_SPRUCE:
            return is_soil(below);
        case B_DEADBUSH:
            return below == B_SAND || is_soil(below);
        case B_MUSHROOM_BROWN: case B_MUSHROOM_RED:
            return block_opaque(below);
        case B_CACTUS:
            if (below != B_CACTUS && below != B_SAND)
                return false;
            for (int f = 0; f < 6; f++) {
                if (f == FACE_U || f == FACE_D)
                    continue;
                if (block_solid(world_get(x + face_dx[f], y, z + face_dz[f])))
                    return false;
            }
            return true;
        case B_TORCH:
            return block_opaque(below) || block_solid(below);
        case B_FIRE:
            return block_solid(below);
        case B_BED + 0: case B_BED + 1: case B_BED + 2: case B_BED + 3:
        case B_BED + 4: case B_BED + 5: case B_BED + 6: case B_BED + 7:
            return block_solid(below);
        default:
            break;
    }
    if (is_wheat(id))
        return below == B_FARMLAND || below == B_FARMLAND_WET;
    if (is_wall_torch(id) || is_ladder(id)) {
        int f = is_ladder(id) ? id - B_LADDER : id - B_TORCH_N;   // facing away from wall
        static const int8_t fdx[4] = { 0, 1, 0, -1 }, fdz[4] = { -1, 0, 1, 0 };
        int wall = world_get(x - fdx[f], y, z - fdz[f]);
        return block_opaque(wall);
    }
    if (is_door(id)) {
        if ((id - B_DOOR) & 8)
            return is_door(below);
        return block_solid(below);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Breaking with drops
// ---------------------------------------------------------------------------

static void drop(int x, int y, int z, int id, int count)
{
    if (!id || count <= 0)
        return;
    ItemStack s = { .id = id, .count = count };
    entity_spawn_item((x << 12) + 2048, (y << 12) + 1024, (z << 12) + 2048, &s);
}

void sim_break_block(int x, int y, int z, bool drops, int tool_item)
{
    int id = world_get(x, y, z);
    if (id == B_AIR)
        return;
    const BlockDef *b = &blocks[id];
    int replace = B_AIR;
    if (drops) {
        if (is_leaves(id)) {
            if (tool_item == I_SHEARS) {
                drop(x, y, z, id, 1);
            } else {
                if (rng_range(20) == 0)
                    drop(x, y, z, B_SAPLING_OAK + (id - B_LEAVES_OAK), 1);
                if (id == B_LEAVES_OAK && rng_range(120) == 0)
                    drop(x, y, z, I_APPLE, 1);
            }
        } else if (id == B_TALLGRASS) {
            if (rng_range(8) == 0)
                drop(x, y, z, I_SEEDS, 1);
        } else if (id == B_GRAVEL) {
            drop(x, y, z, rng_range(10) == 0 ? I_FLINT : B_GRAVEL, 1);
        } else if (is_wheat(id)) {
            if (id == B_WHEAT_LAST) {
                drop(x, y, z, I_WHEAT, 1);
                drop(x, y, z, I_SEEDS, 1 + rng_range(3));
            } else {
                drop(x, y, z, I_SEEDS, 1);
            }
        } else if (id == B_ICE) {
            replace = world_get(x, y - 1, z) != B_AIR ? B_WATER : B_AIR;
        } else if (id == B_REDSTONE_ORE) {
            drop(x, y, z, I_REDSTONE, 4 + rng_range(2));
        } else if (b->drop) {
            drop(x, y, z, b->drop, b->drop_count);
        }
    }
    if (is_chest(id))
        chest_remove(x, y, z);
    if (is_furnace(id))
        furnace_remove(x, y, z);
    // multi-block structures
    if (is_door(id)) {
        int other_y = ((id - B_DOOR) & 8) ? y - 1 : y + 1;
        if (is_door(world_get(x, other_y, z))) {
            world_set(x, other_y, z, B_AIR);
            sim_notify(x, other_y, z);
        }
        if (drops && ((id - B_DOOR) & 8))
            drop(x, y, z, I_DOOR, 1);
    }
    if (is_bed(id)) {
        int idx = id - B_BED;
        int f = idx & 3;
        static const int8_t fdx[4] = { 0, 1, 0, -1 }, fdz[4] = { -1, 0, 1, 0 };
        int head = idx & 4;
        int ox = head ? x - fdx[f] : x + fdx[f];
        int oz = head ? z - fdz[f] : z + fdz[f];
        if (is_bed(world_get(ox, y, oz))) {
            world_set(ox, y, oz, B_AIR);
            sim_notify(ox, y, oz);
        }
        if (drops && head)
            drop(x, y, z, I_BED, 1);
    }
    if (is_log(id)) {
        // schedule leaf decay checks around
        for (int i = 0; i < 12; i++) {
            int lx = x + rng_range(9) - 4, ly = y + rng_range(7) - 2, lz = z + rng_range(9) - 4;
            if (is_leaves(world_get(lx, ly, lz)))
                sim_schedule(lx, ly, lz, 20 + rng_range(200));
        }
    }
    if (id == B_TNT && !drops && tool_item == -1) {
        // exploded
    }
    world_set(x, y, z, replace);
    sim_notify(x, y, z);
}

// ---------------------------------------------------------------------------
// Liquids
// ---------------------------------------------------------------------------

static int liquid_base(int id)
{
    return is_water(id) ? B_WATER : B_LAVA;
}

static bool same_liquid(int a, int b)
{
    return (is_water(a) && is_water(b)) || (is_lava(a) && is_lava(b));
}

static int flowing_id(int base, int level)
{
    if (level == 8)
        return base == B_WATER ? B_WATER_FALL : B_LAVA_FALL;
    if (base == B_WATER)
        return B_WATER + level;
    // lava levels 2,4,6 -> F1..F3
    int l = (level + 1) / 2;
    if (l < 1) l = 1;
    if (l > 3) l = 3;
    return B_LAVA + l;
}

static bool liquid_can_replace(int target, int liquid_id, int new_level)
{
    if (target == B_AIR)
        return true;
    if (block_liquid(target)) {
        if (!same_liquid(target, liquid_id))
            return false;
        int lv = liquid_level(target);
        if (lv == 0)
            return false;
        if (new_level == 8)
            return lv != 8;
        return lv != 8 && lv > new_level;
    }
    const BlockDef *b = &blocks[target];
    if (b->flags & BF_SOLID)
        return false;
    if (target == B_FIRE)
        return true;
    // plants and torches get washed away
    return b->shape == SHAPE_CROSS || b->shape == SHAPE_TORCH || b->shape == SHAPE_CROPS;
}

static void mix_lava_water(int x, int y, int z, int id)
{
    // lava touching water hardens
    for (int f = 0; f < 6; f++) {
        if (f == FACE_D)
            continue;
        int n = world_get(x + face_dx[f], y + face_dy[f], z + face_dz[f]);
        if (is_lava(id) && is_water(n)) {
            world_set(x, y, z, liquid_level(id) == 0 ? B_OBSIDIAN : B_COBBLESTONE);
            sound_play_at(SFX_FIRE, x << 12, y << 12, z << 12, 90, 150);
            return;
        }
    }
}

static void update_liquid(int x, int y, int z, int id)
{
    int base = liquid_base(id);
    int level = liquid_level(id);
    int step = (base == B_WATER) ? 1 : 2;
    if (is_lava(id)) {
        mix_lava_water(x, y, z, id);
        id = world_get(x, y, z);
        if (!block_liquid(id))
            return;
    }
    // Recompute level for flowing liquids
    if (level != 0) {
        int above = world_get(x, y + 1, z);
        int new_level;
        if (same_liquid(above, id)) {
            new_level = 8;
        } else {
            int best = 99, sources = 0;
            for (int f = 0; f < 6; f++) {
                if (f == FACE_U || f == FACE_D)
                    continue;
                int n = world_get(x + face_dx[f], y, z + face_dz[f]);
                if (!same_liquid(n, id))
                    continue;
                int nl = liquid_level(n);
                if (nl == 0)
                    sources++;
                int cand = (nl == 8 ? 0 : nl) + step;
                if (cand < best)
                    best = cand;
            }
            int bl = world_get(x, y - 1, z);
            if (base == B_WATER && sources >= 2 && (block_solid(bl) || (is_water(bl) && liquid_level(bl) == 0)))
                new_level = 0;
            else if (best > 7)
                new_level = -1;
            else
                new_level = best;
        }
        if (new_level < 0) {
            world_set(x, y, z, B_AIR);
            sim_notify(x, y, z);
            return;
        }
        int nid = new_level == 0 ? base : flowing_id(base, new_level);
        if (nid != id) {
            world_set(x, y, z, nid);
            sim_notify(x, y, z);
            id = nid;
            level = new_level;
        }
    }
    // Spread downward
    int below = world_get(x, y - 1, z);
    if (y > 0 && liquid_can_replace(below, id, 8)) {
        if (below != B_AIR && !block_liquid(below))
            sim_break_block(x, y - 1, z, true, 0);
        world_set(x, y - 1, z, flowing_id(base, 8));
        sim_notify(x, y - 1, z);
        return;
    }
    if (is_lava(below) && is_water(id)) {
        world_set(x, y - 1, z, liquid_level(below) == 0 ? B_OBSIDIAN : B_STONE);
        return;
    }
    // Spread sideways when resting on something
    int next = (level == 8 || level == 0) ? step : level + step;
    if (next > 7)
        return;
    if (same_liquid(below, id) && liquid_level(below) == 8)
        return;
    for (int f = 0; f < 6; f++) {
        if (f == FACE_U || f == FACE_D)
            continue;
        int nx = x + face_dx[f], nz = z + face_dz[f];
        int n = world_get(nx, y, nz);
        if (is_water(id) && is_lava(n)) {
            world_set(nx, y, nz, liquid_level(n) == 0 ? B_OBSIDIAN : B_COBBLESTONE);
            continue;
        }
        if (!liquid_can_replace(n, id, next))
            continue;
        if (n != B_AIR && !block_liquid(n))
            sim_break_block(nx, y, nz, true, 0);
        world_set(nx, y, nz, flowing_id(base, next));
        sim_notify(nx, y, nz);
    }
}

// ---------------------------------------------------------------------------
// Scheduled update dispatch
// ---------------------------------------------------------------------------

static bool log_nearby(int x, int y, int z)
{
    for (int dy = -3; dy <= 3; dy++)
        for (int dz = -4; dz <= 4; dz++)
            for (int dx = -4; dx <= 4; dx++)
                if (iabs(dx) + iabs(dy) + iabs(dz) <= 5 && is_log(world_get(x + dx, y + dy, z + dz)))
                    return true;
    return false;
}

static void update_block(int x, int y, int z)
{
    int id = world_get(x, y, z);
    const BlockDef *b = &blocks[id];
    if (id == B_AIR)
        return;
    if (block_liquid(id)) {
        update_liquid(x, y, z, id);
        return;
    }
    if ((b->flags & BF_NEEDS_SUPPORT) && !sim_can_stay(x, y, z, id)) {
        if (is_door(id) && ((id - B_DOOR) & 8)) {
            world_set(x, y, z, B_AIR);
            sim_notify(x, y, z);
        } else {
            particles_block(x, y, z, id);
            sim_break_block(x, y, z, true, 0);
        }
        return;
    }
    if (is_door(id) && !sim_can_stay(x, y, z, id)) {
        sim_break_block(x, y, z, !((id - B_DOOR) & 8), 0);
        return;
    }
    if (b->flags & BF_GRAVITY) {
        int below = world_get(x, y - 1, z);
        if (y > 0 && (below == B_AIR || block_liquid(below) || (blocks[below].flags & BF_REPLACEABLE))) {
            if (below != B_AIR && !block_liquid(below))
                sim_break_block(x, y - 1, z, true, 0);
            world_set(x, y - 1, z, id);
            world_set(x, y, z, B_AIR);
            sim_notify(x, y, z);
            sim_schedule(x, y - 1, z, 2);
        }
        return;
    }
    if (id == B_FIRE) {
        // burn out, possibly spreading and consuming flammable blocks
        int below = world_get(x, y - 1, z);
        bool eternal = below == B_COAL_BLOCK || below == B_OBSIDIAN;
        for (int f = 0; f < 6; f++) {
            int nx = x + face_dx[f], ny = y + face_dy[f], nz = z + face_dz[f];
            int n = world_get(nx, ny, nz);
            if ((blocks[n].flags & BF_FLAMMABLE) && rng_range(4) == 0) {
                if (n == B_TNT) {
                    world_set(nx, ny, nz, B_AIR);
                    int e = entity_spawn(ET_TNT, (nx << 12) + 2048, ny << 12, (nz << 12) + 2048);
                    if (e >= 0)
                        entities[e].fuse = 40;
                } else {
                    world_set(nx, ny, nz, rng_range(2) ? B_FIRE : B_AIR);
                    sim_schedule(nx, ny, nz, 30 + rng_range(40));
                    sim_notify(nx, ny, nz);
                }
            }
        }
        if (!eternal && (rng_range(3) == 0 || !block_solid(below))) {
            world_set(x, y, z, B_AIR);
            sim_notify(x, y, z);
        } else {
            sim_schedule(x, y, z, 30 + rng_range(30));
        }
        return;
    }
    if (is_leaves(id)) {
        if (!log_nearby(x, y, z)) {
            sim_break_block(x, y, z, true, 0);
        }
        return;
    }
    if (id == B_CACTUS && !sim_can_stay(x, y, z, id)) {
        sim_break_block(x, y, z, true, 0);
        return;
    }
}

static void random_tick_block(int x, int y, int z)
{
    int id = world_get(x, y, z);
    switch (id) {
        case B_GRASS: case B_SNOWY_GRASS: {
            int above = world_get(x, y + 1, z);
            int la = world_light_raw(x, y + 1, z);
            int light = imax(la >> 4, la & 15);
            if (block_opaque(above) || (light < 4 && block_liquid(above))) {
                world_set(x, y, z, B_DIRT);
                return;
            }
            if (light >= 9) {
                int tx = x + rng_range(3) - 1, ty = y + rng_range(5) - 3, tz = z + rng_range(3) - 1;
                if (world_get(tx, ty, tz) == B_DIRT && !block_opaque(world_get(tx, ty + 1, tz)) &&
                    !block_liquid(world_get(tx, ty + 1, tz))) {
                    int l2 = world_light_raw(tx, ty + 1, tz);
                    if (imax(l2 >> 4, l2 & 15) >= 4)
                        world_set(tx, ty, tz, worldgen_biome(tx, tz) == BIOME_SNOWY ? B_SNOWY_GRASS : B_GRASS);
                }
            }
            break;
        }
        case B_SAPLING_OAK: case B_SAPLING_BIRCH: case B_SAPLING_SPRUCE: {
            int l = world_light_raw(x, y, z);
            if (imax(l >> 4, l & 15) >= 9 && rng_range(7) == 0)
                worldgen_grow_tree(x, y, z, id - B_SAPLING_OAK);
            break;
        }
        case B_FARMLAND: case B_FARMLAND_WET: {
            bool wet = false;
            for (int dz = -4; dz <= 4 && !wet; dz++)
                for (int dx = -4; dx <= 4 && !wet; dx++)
                    for (int dy = 0; dy <= 1; dy++)
                        if (is_water(world_get(x + dx, y + dy, z + dz))) {
                            wet = true;
                            break;
                        }
            if (wet && id == B_FARMLAND)
                world_set(x, y, z, B_FARMLAND_WET);
            else if (!wet && id == B_FARMLAND_WET)
                world_set(x, y, z, B_FARMLAND);
            else if (!wet && !is_wheat(world_get(x, y + 1, z)) && rng_range(4) == 0)
                world_set(x, y, z, B_DIRT);
            break;
        }
        case B_CACTUS: {
            if (world_get(x, y + 1, z) == B_AIR && rng_range(8) == 0) {
                int h = 1;
                while (h < 4 && world_get(x, y - h, z) == B_CACTUS)
                    h++;
                if (h < 3 && sim_can_stay(x, y + 1, z, B_CACTUS))
                    world_set(x, y + 1, z, B_CACTUS);
            }
            break;
        }
        default:
            if (is_wheat(id) && id != B_WHEAT_LAST) {
                int l = world_light_raw(x, y + 1, z);
                if (imax(l >> 4, l & 15) >= 9) {
                    int chance = world_get(x, y - 1, z) == B_FARMLAND_WET ? 3 : 8;
                    if (rng_range(chance) == 0)
                        world_set(x, y, z, id + 1);
                }
            }
            break;
    }
}

bool sim_try_sleep(void)
{
    int32_t t = world.time;
    // anchor spawn at the bed position
    player.spawn_x = player.tx;
    player.spawn_y = player.ty + 1;
    player.spawn_z = player.tz;
    player.has_bed_spawn = true;
    if (t < 12500 || t > 23400) {
        game_message("You can only sleep at night", 60);
        game_message("Spawn point set", 60);
        return false;
    }
    for (int i = 0; i < MAX_ENTITIES; i++) {
        Entity *e = &entities[i];
        if (e->type >= ET_ZOMBIE && e->type <= ET_SHROOM) {
            int32_t dx = iabs(world_wrap_delta_fx(e->body.x - player.body.x));
            int32_t dz = iabs(world_wrap_delta_fx(e->body.z - player.body.z));
            if (dx < FX(8) && dz < FX(8)) {
                game_message("You may not rest now, there are monsters nearby", 80);
                return false;
            }
        }
    }
    player.sleep_timer = 60;
    return true;
}

void sim_tick(void)
{
    sim_now++;
    // Scheduled updates due this tick
    int processed = 0;
    int i = 0;
    while (i < nupdates && processed < 256) {
        Update u = updates[i];
        if ((int16_t)(sim_now - u.due) >= 0) {
            updates[i] = updates[--nupdates];
            update_block(u.x, u.y, u.z);
            processed++;
        } else {
            i++;
        }
    }
    // Random ticks around the player
    int pcx = (player.body.x >> 16) & (world.chunks - 1);
    int pcz = (player.body.z >> 16) & (world.chunks - 1);
    for (int dz = -3; dz <= 3; dz++) {
        for (int dx = -3; dx <= 3; dx++) {
            int cx = (pcx + dx) & (world.chunks - 1), cz = (pcz + dz) & (world.chunks - 1);
            for (int k = 0; k < 2; k++) {
                uint32_t r = rng_next();
                int x = cx * 16 + (r & 15), z = cz * 16 + ((r >> 4) & 15), y = (r >> 8) % WORLD_H;
                random_tick_block(x, y, z);
            }
        }
    }
}
