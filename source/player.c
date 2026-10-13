// Player physics, survival stats and interaction with the world
#include <stdio.h>
#include "player.h"
#include "game.h"
#include "world.h"
#include "blocks.h"
#include "render.h"
#include "entity.h"
#include "sim.h"
#include "sound.h"
#include "ui.h"
#include "worldgen.h"

Player player;
const char *death_message = "";

#define EYE_HEIGHT 6636     // 1.62 blocks
#define SNEAK_EYE  6308     // 1.54
#define PLAYER_HW  1229     // 0.3
#define PLAYER_H   7373     // 1.8

ItemStack *player_held(void)
{
    return &player.inv[player.sel];
}

int player_facing(void)
{
    int a = (player.yaw + ANG_FULL / 8) & (ANG_FULL - 1);
    return a / (ANG_FULL / 4);
}

void player_reset(int mode)
{
    memset(&player, 0, sizeof(player));
    player.mode = mode;
    player.health = 20;
    player.food = 20;
    player.saturation = 5 << 12;
    player.air = 300;
    player.body.hw = PLAYER_HW;
    player.body.h = PLAYER_H;
    player.target_entity = -1;
    player.spawn_x = world.spawn_x;
    player.spawn_y = world.spawn_y;
    player.spawn_z = world.spawn_z;
    if (mode == MODE_CREATIVE) {
        static const uint16_t kit[9] = { B_GRASS, B_COBBLESTONE, B_PLANKS_OAK, B_GLASS, B_BRICKS, B_TORCH,
                                         B_LOG_OAK, B_STONEBRICKS, B_WOOL_WHITE + 14 };
        for (int i = 0; i < 9; i++) {
            player.inv[i].id = kit[i];
            player.inv[i].count = 64;
        }
    }
}

void player_spawn(void)
{
    int x = player.spawn_x, y = player.spawn_y, z = player.spawn_z;
    // make sure the spawn point is free
    for (int tries = 0; tries < WORLD_H; tries++) {
        if (!block_solid(world_get(x, y, z)) && !block_solid(world_get(x, y + 1, z)))
            break;
        y++;
    }
    player.body.x = (x << 12) + 2048;
    player.body.y = y << 12;
    player.body.z = (z << 12) + 2048;
    player.prev_x = player.body.x;
    player.prev_y = player.body.y;
    player.prev_z = player.body.z;
    player.vx = player.vy = player.vz = 0;
    player.fall_dist = 0;
    player.body.on_ground = false;
}

void player_respawn(void)
{
    player.dead = false;
    player.health = 20;
    player.food = 20;
    player.saturation = 5 << 12;
    player.exhaustion = 0;
    player.air = 300;
    player.fire = 0;
    player.invuln = 40;
    if (player.has_bed_spawn && !is_bed(world_get(player.spawn_x, player.spawn_y - 1, player.spawn_z)) &&
        !is_bed(world_get(player.spawn_x, player.spawn_y, player.spawn_z))) {
        player.has_bed_spawn = false;
        game_message("Your home bed was missing", 80);
        player.spawn_x = world.spawn_x;
        player.spawn_y = world.spawn_y;
        player.spawn_z = world.spawn_z;
    }
    player_spawn();
}

int player_armor_points(void)
{
    int p = 0;
    for (int i = 0; i < 4; i++)
        if (!stack_empty(&player.armor[i]))
            p += items[player.armor[i].id].armor_points;
    return p;
}

void player_add_exhaustion(int32_t fx)
{
    if (player.mode == MODE_CREATIVE)
        return;
    player.exhaustion += fx;
}

static void drop_all_items(void)
{
    for (int i = 0; i < INV_SIZE; i++)
        player_drop_stack(&player.inv[i]);
    for (int i = 0; i < 4; i++)
        player_drop_stack(&player.armor[i]);
    for (int i = 0; i < 4; i++)
        player_drop_stack(&player.craft[i]);
    player_drop_stack(&player.cursor);
}

void player_damage(int amount, int cause)
{
    if (player.mode == MODE_CREATIVE && cause != DMG_VOID)
        return;
    if (player.dead || amount <= 0)
        return;
    if (player.invuln > 0 && cause != DMG_VOID && cause != DMG_DROWN && cause != DMG_STARVE)
        return;
    // armor reduces physical damage
    if (cause == DMG_MOB || cause == DMG_EXPLOSION || cause == DMG_ARROW || cause == DMG_GENERIC || cause == DMG_CACTUS) {
        int ap = player_armor_points();
        int reduced = amount * (25 - ap) / 25;
        if (reduced < 1 && amount > 0)
            reduced = (ap >= 20) ? 0 : 1;
        // wear armor
        for (int i = 0; i < 4; i++) {
            ItemStack *a = &player.armor[i];
            if (!stack_empty(a)) {
                a->damage += 1;
                if (a->damage >= items[a->id].durability) {
                    stack_clear(a);
                    sound_play(SFX_DIG_GLASS, 90, 64, 120);
                }
            }
        }
        amount = reduced;
    }
    player.health -= amount;
    player.hurt_time = 10;
    player.invuln = 10;
    player_add_exhaustion(FX(0.1));
    sound_play(SFX_HURT, 110, 64, 100);
    if (player.health <= 0) {
        player.health = 0;
        player.dead = true;
        switch (cause) {
            case DMG_FALL: death_message = "You hit the ground too hard"; break;
            case DMG_DROWN: death_message = "You drowned"; break;
            case DMG_LAVA: death_message = "You tried to swim in lava"; break;
            case DMG_FIRE: death_message = "You burned to death"; break;
            case DMG_MOB: death_message = "You were slain"; break;
            case DMG_EXPLOSION: death_message = "You blew up"; break;
            case DMG_STARVE: death_message = "You starved to death"; break;
            case DMG_CACTUS: death_message = "You were pricked to death"; break;
            case DMG_ARROW: death_message = "You were shot"; break;
            case DMG_VOID: death_message = "You fell out of the world"; break;
            default: death_message = "You died"; break;
        }
        drop_all_items();
        ui_open(UI_DEATH);
    }
}

void player_heal(int amount)
{
    player.health += amount;
    if (player.health > 20)
        player.health = 20;
}

bool player_give(int id, int count, int damage)
{
    int left = inv_add(player.inv, INV_SIZE, id, count, damage);
    if (left > 0) {
        ItemStack s = { .id = id, .count = left, .damage = damage };
        player_drop_stack(&s);
        return false;
    }
    return true;
}

void player_drop_stack(ItemStack *s)
{
    if (stack_empty(s))
        return;
    int32_t dx, dy, dz;
    camera_forward(&dx, &dy, &dz);
    int32_t x = player.body.x, y = player.body.y + EYE_HEIGHT - 1229, z = player.body.z;
    entity_spawn_item_vel(x, y, z, s, fxmul(dx, 1200), fxmul(dy, 1200) + 400, fxmul(dz, 1200));
    // delay pickup for thrown items
    for (int i = 0; i < MAX_ENTITIES; i++)
        if (entities[i].type == ET_ITEM && entities[i].age == 0)
            entities[i].pickup_delay = 40;
    stack_clear(s);
}

void player_drop_held(bool whole_stack)
{
    ItemStack *h = player_held();
    if (stack_empty(h))
        return;
    ItemStack s = *h;
    if (!whole_stack) {
        s.count = 1;
        h->count--;
        if (h->count == 0)
            stack_clear(h);
    } else {
        stack_clear(h);
    }
    player_drop_stack(&s);
}

bool player_eye_in(uint16_t flag)
{
    int32_t ey = player.body.y + (player.sneaking ? SNEAK_EYE : EYE_HEIGHT);
    int id = world_get(player.body.x >> 12, ey >> 12, player.body.z >> 12);
    if (flag == BF_LIQUID && block_liquid(id)) {
        int32_t top = ((ey >> 12) << 12) + liquid_height16(id) * 256;
        return ey < top;
    }
    return (blocks[id].flags & flag) != 0;
}

// ---------------------------------------------------------------------------
// Raycast
// ---------------------------------------------------------------------------

static bool raycast_block(int32_t ox, int32_t oy, int32_t oz, int32_t dx, int32_t dy, int32_t dz, int32_t maxd,
                          bool liquids, int *hx, int *hy, int *hz, int *hface, int32_t *ht)
{
    int x = ox >> 12, y = oy >> 12, z = oz >> 12;
    int sx = dx > 0 ? 1 : -1, sy = dy > 0 ? 1 : -1, sz = dz > 0 ? 1 : -1;
    const int32_t BIG = 0x7FFFFFFF;
    int32_t adx = iabs(dx), ady = iabs(dy), adz = iabs(dz);
    int32_t tdx = adx ? (int32_t)(((int64_t)4096 << 12) / adx) : BIG;
    int32_t tdy = ady ? (int32_t)(((int64_t)4096 << 12) / ady) : BIG;
    int32_t tdz = adz ? (int32_t)(((int64_t)4096 << 12) / adz) : BIG;
    int32_t fx = ox & 4095, fy = oy & 4095, fz = oz & 4095;
    int32_t tmx = adx ? (int32_t)(((int64_t)(dx > 0 ? 4096 - fx : fx) << 12) / adx) : BIG;
    int32_t tmy = ady ? (int32_t)(((int64_t)(dy > 0 ? 4096 - fy : fy) << 12) / ady) : BIG;
    int32_t tmz = adz ? (int32_t)(((int64_t)(dz > 0 ? 4096 - fz : fz) << 12) / adz) : BIG;
    int face = -1;
    int32_t t = 0;
    for (int i = 0; i < 64; i++) {
        if (y >= 0 && y < WORLD_H) {
            int id = world_get(x, y, z);
            const BlockDef *b = &blocks[id];
            bool hit = (b->flags & BF_SELECTABLE) != 0;
            if (liquids && block_liquid(id) && liquid_level(id) == 0)
                hit = true;
            if (hit && face >= 0) {
                *hx = x; *hy = y; *hz = z; *hface = face; *ht = t;
                return true;
            }
        }
        if (tmx < tmy && tmx < tmz) {
            t = tmx; tmx += tdx; x += sx; face = sx > 0 ? FACE_W : FACE_E;
        } else if (tmy < tmz) {
            t = tmy; tmy += tdy; y += sy; face = sy > 0 ? FACE_D : FACE_U;
        } else {
            t = tmz; tmz += tdz; z += sz; face = sz > 0 ? FACE_N : FACE_S;
        }
        if (t > maxd)
            break;
    }
    return false;
}

static void eye_pos(int32_t *x, int32_t *y, int32_t *z)
{
    *x = player.body.x;
    *y = player.body.y + (player.sneaking ? SNEAK_EYE : EYE_HEIGHT);
    *z = player.body.z;
}

static void update_target(void)
{
    int32_t ex, ey, ez, dx, dy, dz;
    eye_pos(&ex, &ey, &ez);
    camera_forward(&dx, &dy, &dz);
    int32_t reach = player.mode == MODE_CREATIVE ? FX(5) : FX(4.5);
    ItemStack *h = player_held();
    bool liquids = h->id == I_BUCKET;
    int32_t bt = reach;
    player.target_valid = raycast_block(ex, ey, ez, dx, dy, dz, reach, liquids,
                                        &player.tx, &player.ty, &player.tz, &player.tface, &bt);
    if (!player.target_valid)
        bt = reach;
    int32_t et;
    int e = entity_raycast(ex, ey, ez, dx, dy, dz, imin(bt, FX(3.5)), &et);
    player.target_entity = e;
    if (e >= 0)
        player.target_valid = false;
}

// ---------------------------------------------------------------------------
// Block breaking
// ---------------------------------------------------------------------------

static int32_t break_speed(int block_id)
{
    // returns damage per tick in fx (block breaks at 4096)
    const BlockDef *b = &blocks[block_id];
    if (b->hardness == 0xFFFF)
        return 0;
    if (b->hardness == 0)
        return 4096;
    ItemStack *h = player_held();
    const ItemDef *it = item_valid(h->id) ? &items[h->id] : NULL;
    int speed = 1;
    bool right_tool = false;
    if (it && it->kind == IK_TOOL && it->tool_type == b->tool && b->tool != TOOL_NONE) {
        speed = it->speed;
        right_tool = true;
    }
    if (it && it->tool_type == TOOL_SWORD && is_leaves(block_id))
        speed = 2;
    if (it && h->id == I_SHEARS && (is_leaves(block_id) || (block_id >= B_WOOL_WHITE && block_id <= B_WOOL_LAST)))
        speed = 5, right_tool = true;
    bool can_harvest = b->tier == 0 || (right_tool && it->tool_tier >= b->tier);
    if (b->tool == TOOL_SHEARS)
        can_harvest = true;
    int32_t s = (int32_t)speed << 12;
    if (player.in_water && player.head_in_water)
        s /= 5;
    if (!player.body.on_ground && !player.flying && !player.on_ladder)
        s /= 5;
    // damage per tick = speed / hardness / (harvestable ? 30 : 100); hardness stored x10
    int div = can_harvest ? 30 : 100;
    return (s * 10) / (b->hardness * div);
}

static bool harvestable(int block_id)
{
    const BlockDef *b = &blocks[block_id];
    if (b->tier == 0)
        return true;
    ItemStack *h = player_held();
    if (!item_valid(h->id))
        return false;
    const ItemDef *it = &items[h->id];
    return it->kind == IK_TOOL && it->tool_type == b->tool && it->tool_tier >= b->tier;
}

static void damage_held_tool(int amount)
{
    if (player.mode == MODE_CREATIVE)
        return;
    ItemStack *h = player_held();
    if (!item_valid(h->id) || !items[h->id].durability)
        return;
    h->damage += amount;
    if (h->damage >= items[h->id].durability) {
        stack_clear(h);
        sound_play(SFX_DIG_GLASS, 100, 64, 70);
        game_message("Your tool broke!", 40);
    }
}

static void break_target(void)
{
    int x = player.tx, y = player.ty, z = player.tz;
    int id = world_get(x, y, z);
    if (blocks[id].hardness == 0xFFFF && player.mode != MODE_CREATIVE)
        return;
    if (id == B_BEDROCK && player.mode != MODE_CREATIVE)
        return;
    bool drops = player.mode != MODE_CREATIVE && harvestable(id);
    ItemStack *h = player_held();
    particles_block(x, y, z, id);
    sound_block(id, false, (x << 12) + 2048, (y << 12) + 2048, (z << 12) + 2048);
    sim_break_block(x, y, z, drops, h->id);
    if (player.mode != MODE_CREATIVE) {
        if (blocks[id].hardness > 0)
            damage_held_tool(1);
        player_add_exhaustion(FX(0.005));
    }
    player.swing = 6;
}

// ---------------------------------------------------------------------------
// Placing / using
// ---------------------------------------------------------------------------

static bool body_intersects_cell(const Body *b, int x, int y, int z)
{
    int32_t x0 = x << 12, y0 = y << 12, z0 = z << 12;
    int32_t bx0 = b->x - b->hw, bx1 = b->x + b->hw;
    int32_t bz0 = b->z - b->hw, bz1 = b->z + b->hw;
    // wrap the cell near the body
    int32_t ddx = world_wrap_delta_fx(x0 - bx0);
    int32_t ddz = world_wrap_delta_fx(z0 - bz0);
    x0 = bx0 + ddx;
    z0 = bz0 + ddz;
    return bx1 > x0 && bx0 < x0 + 4096 && b->y + b->h > y0 && b->y < y0 + 4096 && bz1 > z0 && bz0 < z0 + 4096;
}

static bool cell_blocked_by_entity(int x, int y, int z)
{
    if (body_intersects_cell(&player.body, x, y, z))
        return true;
    for (int i = 0; i < MAX_ENTITIES; i++) {
        Entity *e = &entities[i];
        if (e->type == ET_NONE || e->type == ET_ITEM || e->type == ET_ARROW)
            continue;
        if (body_intersects_cell(&e->body, x, y, z))
            return true;
    }
    return false;
}

static void consume_held(void)
{
    if (player.mode == MODE_CREATIVE)
        return;
    ItemStack *h = player_held();
    h->count--;
    if (h->count == 0)
        stack_clear(h);
}

static bool try_place_block(int bid, int x, int y, int z, int face)
{
    if (y < 0 || y >= WORLD_H)
        return false;
    int cur = world_get(x, y, z);
    if (!block_replaceable(cur))
        return false;
    if (cur == bid)
        return false;
    const BlockDef *b = &blocks[bid];
    if ((b->flags & BF_SOLID) && cell_blocked_by_entity(x, y, z))
        return false;
    int place = bid;
    int pf = player_facing();
    int toward = (pf + 2) & 3;
    if (bid == B_TORCH) {
        if (face == FACE_U || face == FACE_D) {
            if (!sim_can_stay(x, y, z, B_TORCH)) {
                // try wall attachment in looking direction
                return false;
            }
            place = B_TORCH;
        } else {
            static const uint8_t face_to_facing[6] = { 3, 1, 0, 0, 0, 2 };
            place = B_TORCH_N + face_to_facing[face];
        }
    } else if (bid == B_LADDER) {
        if (face == FACE_U || face == FACE_D)
            return false;
        static const uint8_t face_to_facing[6] = { 3, 1, 0, 0, 0, 2 };
        place = B_LADDER + face_to_facing[face];
    } else if (bid == B_CHEST || bid == B_FURNACE || bid == B_PUMPKIN || bid == B_JACKOLANTERN) {
        place = bid + toward;
    }
    if (!sim_can_stay(x, y, z, place))
        return false;
    world_set(x, y, z, place);
    sim_notify(x, y, z);
    if (is_chest(place))
        chest_get(x, y, z, true);
    if (is_furnace(place))
        furnace_get(x, y, z, true);
    sound_block(place, false, (x << 12) + 2048, (y << 12) + 2048, (z << 12) + 2048);
    player.swing = 6;
    return true;
}

static bool place_door(int x, int y, int z)
{
    if (!block_replaceable(world_get(x, y, z)) || !block_replaceable(world_get(x, y + 1, z)))
        return false;
    if (!block_solid(world_get(x, y - 1, z)))
        return false;
    if (cell_blocked_by_entity(x, y, z) || cell_blocked_by_entity(x, y + 1, z))
        return false;
    int f = player_facing();
    // panel on the far side of the cell from the player
    world_set(x, y, z, B_DOOR + f);
    world_set(x, y + 1, z, B_DOOR + f + 8);
    sound_play(SFX_DIG_WOOD, 100, 64, 100);
    return true;
}

static bool place_bed(int x, int y, int z)
{
    int f = player_facing();
    static const int8_t fdx[4] = { 0, 1, 0, -1 }, fdz[4] = { -1, 0, 1, 0 };
    int hx = x + fdx[f], hz = z + fdz[f];
    if (!block_replaceable(world_get(x, y, z)) || !block_replaceable(world_get(hx, y, hz)))
        return false;
    if (!block_solid(world_get(x, y - 1, z)) || !block_solid(world_get(hx, y - 1, hz)))
        return false;
    if (cell_blocked_by_entity(x, y, z) || cell_blocked_by_entity(hx, y, hz))
        return false;
    world_set(x, y, z, B_BED + f);
    world_set(hx, y, hz, B_BED + f + 4);
    sound_play(SFX_DIG_CLOTH, 100, 64, 100);
    return true;
}

static void open_or_toggle_door(int x, int y, int z, int id)
{
    int idx = id - B_DOOR;
    int top = idx & 8;
    int by = top ? y - 1 : y;
    int bot = world_get(x, by, z), up = world_get(x, by + 1, z);
    if (is_door(bot))
        world_set(x, by, z, bot ^ 4);
    if (is_door(up))
        world_set(x, by + 1, z, up ^ 4);
    sound_play(SFX_DOOR, 100, 64, 100);
}

static bool use_on_block(void)
{
    int x = player.tx, y = player.ty, z = player.tz;
    int id = world_get(x, y, z);
    const BlockDef *b = &blocks[id];
    ItemStack *h = player_held();
    // Interactive blocks (unless sneaking)
    if ((b->flags & BF_INTERACT) && !player.sneaking) {
        if (id == B_CRAFTING_TABLE) {
            ui_open(UI_CRAFTING);
            return true;
        }
        if (is_chest(id)) {
            ui_open_chest(x, y, z);
            return true;
        }
        if (is_furnace(id)) {
            ui_open_furnace(x, y, z);
            return true;
        }
        if (is_door(id)) {
            open_or_toggle_door(x, y, z, id);
            return true;
        }
        if (is_bed(id)) {
            sim_try_sleep();
            return true;
        }
        if (id == B_TNT && h->id == I_FLINT_STEEL) {
            world_set(x, y, z, B_AIR);
            int e = entity_spawn(ET_TNT, (x << 12) + 2048, y << 12, (z << 12) + 2048);
            if (e >= 0)
                entities[e].fuse = 80;
            sound_play(SFX_FUSE, 100, 64, 100);
            damage_held_tool(1);
            return true;
        }
    }
    int px = x + face_dx[player.tface], py = y + face_dy[player.tface], pz = z + face_dz[player.tface];
    if (block_replaceable(id) && !block_liquid(id) && id != B_AIR) {
        px = x; py = y; pz = z;   // replace tall grass directly
    }
    if (stack_empty(h))
        return false;
    const ItemDef *it = &items[h->id];

    // Tools with special uses
    if (it->tool_type == TOOL_HOE && player.tface != FACE_D) {
        if ((id == B_GRASS || id == B_DIRT) && world_get(x, y + 1, z) == B_AIR) {
            world_set(x, y, z, B_FARMLAND);
            sound_play(SFX_DIG_GRASS, 100, 64, 100);
            damage_held_tool(1);
            if (id == B_GRASS && rng_range(8) == 0) {
                ItemStack s = { .id = I_SEEDS, .count = 1 };
                entity_spawn_item((x << 12) + 2048, (y + 1) << 12, (z << 12) + 2048, &s);
            }
            player.swing = 6;
            return true;
        }
        return false;
    }
    if (h->id == I_FLINT_STEEL) {
        if (world_get(px, py, pz) == B_AIR && block_solid(world_get(px, py - 1, pz))) {
            world_set(px, py, pz, B_FIRE);
            sim_schedule(px, py, pz, 40);
            sound_play(SFX_FIRE, 100, 64, 120);
            damage_held_tool(1);
            return true;
        }
        return false;
    }
    if (h->id == I_BONEMEAL) {
        if (id >= B_SAPLING_OAK && id <= B_SAPLING_SPRUCE) {
            worldgen_grow_tree(x, y, z, id - B_SAPLING_OAK);
            consume_held();
            particles_block(x, y, z, B_LEAVES_OAK);
            return true;
        }
        if (is_wheat(id) && id != B_WHEAT_LAST) {
            world_set(x, y, z, B_WHEAT_LAST);
            consume_held();
            return true;
        }
        if (id == B_GRASS && world_get(x, y + 1, z) == B_AIR) {
            for (int i = 0; i < 12; i++) {
                int gx = x + rng_range(7) - 3, gz = z + rng_range(7) - 3;
                int gy = y + 1;
                if (world_get(gx, gy - 1, gz) == B_GRASS && world_get(gx, gy, gz) == B_AIR)
                    world_set(gx, gy, gz, rng_range(6) ? B_TALLGRASS : (rng_range(2) ? B_FLOWER_RED : B_FLOWER_YELLOW));
            }
            consume_held();
            return true;
        }
        return false;
    }
    if (h->id == I_BUCKET) {
        if (block_liquid(id) && liquid_level(id) == 0) {
            world_set(x, y, z, B_AIR);
            sim_notify(x, y, z);
            int filled = is_water(id) ? I_WATER_BUCKET : I_LAVA_BUCKET;
            if (player.mode == MODE_CREATIVE) {
                player_give(filled, 1, 0);
            } else if (h->count == 1) {
                h->id = filled;
            } else {
                h->count--;
                player_give(filled, 1, 0);
            }
            sound_play(SFX_SPLASH, 90, 64, 120);
            return true;
        }
        return false;
    }
    if (h->id == I_WATER_BUCKET || h->id == I_LAVA_BUCKET) {
        int cur = world_get(px, py, pz);
        if (!block_replaceable(cur))
            return false;
        world_set(px, py, pz, h->id == I_WATER_BUCKET ? B_WATER : B_LAVA);
        sim_notify(px, py, pz);
        if (player.mode != MODE_CREATIVE) {
            h->id = I_BUCKET;
            h->damage = 0;
        }
        sound_play(SFX_SPLASH, 90, 64, 90);
        return true;
    }
    if (h->id == I_SEEDS) {
        if ((id == B_FARMLAND || id == B_FARMLAND_WET) && player.tface == FACE_U && world_get(x, y + 1, z) == B_AIR) {
            world_set(x, y + 1, z, B_WHEAT);
            consume_held();
            sound_play(SFX_DIG_GRASS, 80, 64, 120);
            return true;
        }
        return false;
    }
    if (h->id == I_DOOR) {
        if (player.tface != FACE_U)
            return false;
        if (place_door(px, py, pz)) {
            consume_held();
            return true;
        }
        return false;
    }
    if (h->id == I_BED) {
        if (player.tface != FACE_U)
            return false;
        if (place_bed(px, py, pz)) {
            consume_held();
            return true;
        }
        return false;
    }
    if (it->kind == IK_BLOCK && it->places) {
        if (try_place_block(it->places, px, py, pz, player.tface)) {
            consume_held();
            return true;
        }
    }
    return false;
}

static void use_item_in_air(void)
{
    ItemStack *h = player_held();
    if (stack_empty(h))
        return;
    const ItemDef *it = &items[h->id];
    if (it->kind == IK_FOOD) {
        if (h->id == I_MILK_BUCKET) {
            player.fire = 0;
            if (player.mode != MODE_CREATIVE)
                h->id = I_BUCKET;
            sound_play(SFX_BURP, 90, 64, 100);
            return;
        }
        if (player.food < 20 || player.mode == MODE_CREATIVE) {
            player.eat_timer = 32;
        }
        return;
    }
    if (it->kind == IK_ARMOR) {
        int slot = it->armor_slot;
        ItemStack tmp = player.armor[slot];
        player.armor[slot] = *h;
        *h = tmp;
        sound_play(SFX_CLICK, 100, 64, 80);
        return;
    }
    if (h->id == I_BOW) {
        if (player.mode == MODE_CREATIVE || inv_count_item(player.inv, INV_SIZE, I_ARROW) > 0)
            player.bow_timer = 1;
        else
            game_message("You need arrows", 40);
        return;
    }
    if (h->id == I_EGG) {
        // throw: small chance to spawn a chicken
        int32_t ex, ey, ez, dx, dy, dz;
        eye_pos(&ex, &ey, &ez);
        camera_forward(&dx, &dy, &dz);
        if (rng_range(8) == 0) {
            entity_spawn(ET_CHICKEN, ex + dx * 2, ey, ez + dz * 2);
        }
        consume_held();
        sound_play(SFX_POP, 80, 64, 70);
        return;
    }
}

static void finish_eating(void)
{
    ItemStack *h = player_held();
    if (stack_empty(h) || items[h->id].kind != IK_FOOD)
        return;
    const ItemDef *it = &items[h->id];
    player.food = imin(20, player.food + it->food);
    player.saturation = imin(player.food << 12, player.saturation + ((int32_t)it->saturation << 12) / 10);
    sound_play(SFX_BURP, 90, 64, 100);
    if (h->id == I_ROTTEN_FLESH && rng_range(10) < 8)
        game_message("You feel sick...", 40);
    if (h->id == I_MUSHROOM_STEW && player.mode != MODE_CREATIVE) {
        h->id = I_BOWL;
        h->count = 1;
        return;
    }
    consume_held();
}

static void shoot_bow(void)
{
    int32_t ex, ey, ez, dx, dy, dz;
    eye_pos(&ex, &ey, &ez);
    camera_forward(&dx, &dy, &dz);
    if (player.mode != MODE_CREATIVE) {
        if (!inv_remove_item(player.inv, INV_SIZE, I_ARROW, 1))
            return;
        damage_held_tool(1);
    }
    entity_shoot_arrow(ex + (dx >> 2), ey - 410 + (dy >> 2), ez + (dz >> 2), dx, dy, dz, FX(2.2), true);
    sound_play(SFX_BOW, 100, 64, 100);
}

static void attack_target_entity(void)
{
    int e = player.target_entity;
    if (e < 0 || player.attack_cd > 0)
        return;
    ItemStack *h = player_held();
    int dmg = item_valid(h->id) ? items[h->id].damage : 1;
    if (dmg < 1)
        dmg = 1;
    // critical hit when falling
    if (player.vy < 0 && !player.body.on_ground && !player.in_water)
        dmg += dmg / 2 + 1;
    int32_t dx, dy, dz;
    camera_forward(&dx, &dy, &dz);
    entity_damage(e, dmg, dx, dz, true);
    player.attack_cd = 10;
    player.swing = 6;
    player_add_exhaustion(FX(0.1));
    if (item_valid(h->id) && items[h->id].kind == IK_TOOL)
        damage_held_tool(items[h->id].tool_type == TOOL_SWORD ? 1 : 2);
}

// ---------------------------------------------------------------------------
// Per-frame look & targeting
// ---------------------------------------------------------------------------

void player_frame(int vblanks)
{
    (void)vblanks;
    update_target();
    // Break progress overlay
    int crack = -1;
    if (player.mining && player.target_valid && player.mode != MODE_CREATIVE)
        crack = (player.mine_progress * 10) >> 12;
    render_set_target(player.target_valid && !player.dead, player.tx, player.ty, player.tz, crack);
}

// ---------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------

static void survival_tick(void)
{
    if (player.mode == MODE_CREATIVE) {
        player.air = 300;
        player.fire = 0;
        return;
    }
    // Hunger
    if (player.exhaustion >= FX(4)) {
        player.exhaustion -= FX(4);
        if (player.saturation > 0)
            player.saturation = imax(0, player.saturation - FX(1));
        else if (player.food > 0)
            player.food--;
    }
    player.regen_timer++;
    if (player.food >= 18 && player.health < 20) {
        if (player.regen_timer >= 80) {
            player_heal(1);
            player_add_exhaustion(FX(3));
            player.regen_timer = 0;
        }
    } else if (player.food == 0) {
        if (player.regen_timer >= 80) {
            if (player.health > 1)
                player_damage(1, DMG_STARVE);
            player.regen_timer = 0;
        }
    } else {
        player.regen_timer = 0;
    }
    // Air
    if (player.head_in_water) {
        player.air--;
        if (player.air <= -20) {
            player.air = 0;
            player_damage(2, DMG_DROWN);
        }
    } else if (player.air < 300) {
        player.air = imin(300, player.air + 5);
    }
    // Lava / fire
    if (player.in_lava) {
        player_damage(4, DMG_LAVA);
        player.fire = 300;
    }
    if (body_touching(&player.body, BF_HURTS) && !player.in_lava) {
        // cactus or fire block
        int id = world_get(player.body.x >> 12, player.body.y >> 12, player.body.z >> 12);
        if (id == B_FIRE)
            player.fire = imax(player.fire, 160);
        else
            player_damage(1, DMG_CACTUS);
    }
    if (player.fire > 0) {
        if (player.in_water) {
            player.fire = 0;
        } else {
            player.fire--;
            if (player.fire % 20 == 0)
                player_damage(1, DMG_FIRE);
        }
    }
    if (player.body.y < -(16 << 12))
        player_damage(4, DMG_VOID);
}

static void movement_tick(void)
{
    Player *p = &player;
    p->prev_x = p->body.x;
    p->prev_y = p->body.y;
    p->prev_z = p->body.z;

    // Sneak, sprint
    p->sneaking = input.sneak && !p->flying;
    if (input.fwd <= 0 || p->sneaking || (p->food <= 6 && p->mode != MODE_CREATIVE))
        p->sprinting = false;
    if (input.fwd > 0) {
        // double tap forward to sprint handled in input (sprint_tap_timer)
    }

    int32_t sy = sinLerp(p->yaw), cy = cosLerp(p->yaw);
    int32_t mf = input.fwd << 12, ms = input.strafe << 12;
    if (mf && ms) {
        mf = fxmul(mf, 2896);
        ms = fxmul(ms, 2896);
    }
    // world-space wish direction
    int32_t wx = fxmul(sy, mf) + fxmul(cy, ms);
    int32_t wz = -fxmul(cy, mf) + fxmul(sy, ms);

    p->in_water = body_in_water(&p->body);
    p->in_lava = body_in_lava(&p->body);
    p->on_ladder = body_touching(&p->body, BF_CLIMBABLE);
    p->head_in_water = player_eye_in(BF_LIQUID);

    int32_t accel;
    if (p->flying) {
        accel = p->sprinting ? FX(0.1) : FX(0.05);
        p->vx += fxmul(wx, accel);
        p->vz += fxmul(wz, accel);
        if (input.jump)
            p->vy = FX(0.375);
        else if (input.sneak)
            p->vy = -FX(0.375);
        else
            p->vy = fxmul(p->vy, FX(0.6));
        body_move(&p->body, p->vx, p->vy, p->vz, 0);
        if (p->body.on_ground && p->mode == MODE_CREATIVE)
            p->flying = false;
        p->vx = fxmul(p->vx, FX(0.91));
        p->vz = fxmul(p->vz, FX(0.91));
        p->fall_dist = 0;
        return;
    }
    if (p->in_water || p->in_lava) {
        accel = FX(0.02);
        p->vx += fxmul(wx, accel);
        p->vz += fxmul(wz, accel);
        if (input.jump)
            p->vy += FX(0.04);
        bool was_ground = p->body.on_ground;
        body_move(&p->body, p->vx, p->vy, p->vz, FX(0.6));
        int32_t drag = p->in_lava ? FX(0.5) : FX(0.8);
        p->vx = fxmul(p->vx, drag);
        p->vz = fxmul(p->vz, drag);
        p->vy = fxmul(p->vy, drag) - FX(0.02);
        // climb out of water onto ledges
        if ((p->body.hit_x || p->body.hit_z) && input.jump)
            p->vy = FX(0.3);
        (void)was_ground;
        p->fall_dist = 0;
        return;
    }
    // Ground / air
    int32_t slip = p->body.on_ground ? FX(0.546) : FX(0.91);
    accel = p->body.on_ground ? FX(0.1) : FX(0.02);
    if (p->sprinting)
        accel = fxmul(accel, FX(1.3));
    if (p->sneaking)
        accel = fxmul(accel, FX(0.3));
    p->vx += fxmul(wx, accel);
    p->vz += fxmul(wz, accel);

    if (input.jump && p->body.on_ground) {
        p->vy = FX(0.42);
        if (p->sprinting) {
            p->vx += fxmul(sy, FX(0.2));
            p->vz -= fxmul(cy, FX(0.2));
            player_add_exhaustion(FX(0.2));
        } else {
            player_add_exhaustion(FX(0.05));
        }
    }
    // Auto-jump: walking into a one-block step
    if (settings.auto_jump && p->body.on_ground && (input.fwd || input.strafe) && !p->sneaking) {
        int32_t ahead_x = p->body.x + fxmul(wx, FX(0.6));
        int32_t ahead_z = p->body.z + fxmul(wz, FX(0.6));
        int fx = ahead_x >> 12, fz = ahead_z >> 12, fy = p->body.y >> 12;
        int front = world_get(fx, fy, fz);
        int pfx = p->body.x >> 12, pfz = p->body.z >> 12;
        if ((fx != pfx || fz != pfz) && block_solid(front) && blocks[front].shape == SHAPE_CUBE &&
            !block_solid(world_get(fx, fy + 1, fz)) && !block_solid(world_get(fx, fy + 2, fz)) &&
            !block_solid(world_get(pfx, fy + 2, pfz)) && !is_door(front))
            p->vy = FX(0.42);
    }
    if (p->on_ladder) {
        if (p->vy < -FX(0.15))
            p->vy = -FX(0.15);
        if (p->sneaking && p->vy < 0)
            p->vy = 0;
    }

    int32_t mvx = p->vx, mvz = p->vz;
    // Sneaking: don't walk off edges
    if (p->sneaking && p->body.on_ground) {
        Body probe = p->body;
        int32_t step = FX(0.05);
        while (mvx) {
            probe.x = p->body.x + mvx;
            probe.z = p->body.z;
            probe.y = p->body.y - FX(0.6);
            Body t = probe;
            t.h = FX(0.6);
            if (body_collides(&t))
                break;
            if (iabs(mvx) <= step) { mvx = 0; break; }
            mvx += mvx > 0 ? -step : step;
        }
        while (mvz) {
            probe.x = p->body.x + mvx;
            probe.z = p->body.z + mvz;
            probe.y = p->body.y - FX(0.6);
            Body t = probe;
            t.h = FX(0.6);
            if (body_collides(&t))
                break;
            if (iabs(mvz) <= step) { mvz = 0; break; }
            mvz += mvz > 0 ? -step : step;
        }
    }

    int32_t oldy = p->body.y;
    int32_t old_vy = p->vy;
    body_move(&p->body, mvx, p->vy, mvz, FX(0.6));
    if (p->body.hit_x)
        p->vx = 0;
    if (p->body.hit_z)
        p->vz = 0;
    if (p->body.hit_y)
        p->vy = 0;
    // ladder climbing when pushing against it
    if (p->on_ladder && (p->body.hit_x || p->body.hit_z || input.jump))
        p->vy = FX(0.2);

    // Fall damage
    if (p->body.on_ground) {
        if (p->fall_dist > FX(3) && !p->in_water) {
            int dmg = (p->fall_dist - FX(3) + 4095) >> 12;
            int below = world_get(p->body.x >> 12, (p->body.y - 100) >> 12, p->body.z >> 12);
            if (is_bed(below))
                dmg /= 2;
            player_damage(dmg, DMG_FALL);
            sound_block(below, false, p->body.x, p->body.y, p->body.z);
        }
        p->fall_dist = 0;
    } else if (old_vy < 0 && !p->on_ladder) {
        p->fall_dist += oldy - p->body.y;
    } else if (p->on_ladder) {
        p->fall_dist = 0;
    }

    p->vy = fxmul(p->vy - FX(0.08), FX(0.98));
    p->vx = fxmul(p->vx, slip);
    p->vz = fxmul(p->vz, slip);
    if (iabs(p->vx) < 20) p->vx = 0;
    if (iabs(p->vz) < 20) p->vz = 0;

    // footsteps and sprint exhaustion
    int32_t moved = iabs(world_wrap_delta_fx(p->body.x - p->prev_x)) + iabs(world_wrap_delta_fx(p->body.z - p->prev_z));
    if (p->body.on_ground && !p->sneaking) {
        p->step_dist += moved;
        if (p->step_dist > FX(1.7)) {
            p->step_dist = 0;
            int below = world_get(p->body.x >> 12, (p->body.y - 100) >> 12, p->body.z >> 12);
            sound_block(below, true, p->body.x, p->body.y, p->body.z);
        }
    }
    if (p->sprinting)
        player_add_exhaustion(moved / 10);
}

void player_tick(void)
{
    Player *p = &player;
    if (p->dead)
        return;
    if (p->hurt_time > 0)
        p->hurt_time--;
    if (p->invuln > 0)
        p->invuln--;
    if (p->attack_cd > 0)
        p->attack_cd--;
    if (p->swing > 0)
        p->swing--;
    if (p->mine_cooldown > 0)
        p->mine_cooldown--;
    if (p->jump_tap_timer > 0)
        p->jump_tap_timer--;

    // Creative flight toggle: double tap jump
    if (input.jump_pressed && p->mode == MODE_CREATIVE) {
        if (p->jump_tap_timer > 0) {
            p->flying = !p->flying;
            p->vy = 0;
            p->jump_tap_timer = 0;
        } else {
            p->jump_tap_timer = 7;
        }
    }

    movement_tick();
    survival_tick();
    if (p->dead)
        return;

    // Eating
    if (p->eat_timer > 0) {
        p->eat_timer--;
        if ((p->eat_timer & 3) == 0)
            sound_play(SFX_EAT, 70, 64, 90 + rng_range(20));
        if (p->eat_timer == 0)
            finish_eating();
    }
    if (p->bow_timer > 0) {
        p->bow_timer++;
        if (p->bow_timer > 14) {
            p->bow_timer = 0;
            shoot_bow();
        }
    }

    // Actions
    if (input.use_tap) {
        if (p->target_entity >= 0) {
            ItemStack *h = player_held();
            if (!entity_interact(p->target_entity, h))
                attack_target_entity();
        } else if (p->target_valid) {
            if (!use_on_block())
                use_item_in_air();
        } else {
            use_item_in_air();
        }
        input.use_tap = false;
    }

    if (input.action_hold) {
        if (p->target_entity >= 0) {
            attack_target_entity();
            p->mining = false;
        } else if (p->target_valid) {
            if (p->mode == MODE_CREATIVE) {
                if (p->mine_cooldown == 0) {
                    break_target();
                    p->mine_cooldown = 5;
                }
                p->mining = false;
            } else {
                if (!p->mining || p->mine_x != p->tx || p->mine_y != p->ty || p->mine_z != p->tz) {
                    p->mining = true;
                    p->mine_x = p->tx;
                    p->mine_y = p->ty;
                    p->mine_z = p->tz;
                    p->mine_progress = 0;
                }
                int id = world_get(p->tx, p->ty, p->tz);
                p->mine_progress += break_speed(id);
                p->swing = 6;
                if ((frame_counter & 3) == 0 && blocks[id].hardness > 0)
                    sound_block(id, true, (p->tx << 12) + 2048, (p->ty << 12) + 2048, (p->tz << 12) + 2048);
                if (p->mine_progress >= 4096) {
                    break_target();
                    p->mining = false;
                    p->mine_progress = 0;
                }
            }
        } else {
            p->mining = false;
        }
    } else {
        p->mining = false;
        p->mine_progress = 0;
    }
}

void player_camera(int32_t alpha)
{
    Player *p = &player;
    int32_t dx = world_wrap_delta_fx(p->body.x - p->prev_x);
    int32_t dz = world_wrap_delta_fx(p->body.z - p->prev_z);
    int32_t dy = p->body.y - p->prev_y;
    cam.x = world_wrap_fx(p->prev_x + fxmul(dx, alpha));
    cam.y = p->prev_y + fxmul(dy, alpha) + (p->sneaking ? SNEAK_EYE : EYE_HEIGHT);
    cam.z = world_wrap_fx(p->prev_z + fxmul(dz, alpha));
    cam.yaw = p->yaw;
    cam.pitch = p->pitch;
    if (p->dead) {
        cam.y = p->body.y + 1024;
    }
}
