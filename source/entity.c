// Entities: dropped items, animals, monsters, arrows, TNT, particles
#include <stdlib.h>
#include "entity.h"
#include "world.h"
#include "blocks.h"
#include "player.h"
#include "render.h"
#include "mesh.h"
#include "sim.h"
#include "sound.h"
#include "gfx.h"
#include "gx.h"
#include "game.h"
#include "worldgen.h"
#include "assets_gen.h"

Entity entities[MAX_ENTITIES];

typedef struct {
    const char *name;
    int16_t hw, h;        // half width, height in 1/16 blocks
    int16_t health;
    int16_t speed;        // fx per tick walking accel
    uint8_t hostile;
} EntityInfo;

static const EntityInfo einfo[ET_COUNT] = {
    [ET_ITEM] = { "Item", 2, 4, 5, 0, 0 },
    [ET_PIG] = { "Pig", 7, 14, 10, 300, 0 },
    [ET_COW] = { "Cow", 7, 22, 10, 260, 0 },
    [ET_SHEEP] = { "Sheep", 7, 20, 8, 300, 0 },
    [ET_CHICKEN] = { "Chicken", 3, 11, 4, 280, 0 },
    [ET_ZOMBIE] = { "Zombie", 5, 31, 20, 360, 1 },
    [ET_SKELETON] = { "Skeleton", 5, 31, 20, 380, 1 },
    [ET_SPIDER] = { "Spider", 11, 14, 16, 480, 1 },
    [ET_SHROOM] = { "Boomshroom", 5, 27, 20, 330, 1 },
    [ET_ARROW] = { "Arrow", 1, 2, 1, 0, 0 },
    [ET_TNT] = { "TNT", 8, 16, 1, 0, 0 },
};

const char *entity_name(int type)
{
    return einfo[type].name;
}

void entities_clear(void)
{
    memset(entities, 0, sizeof(entities));
    particles_clear();
}

int entities_count(int type)
{
    int c = 0;
    for (int i = 0; i < MAX_ENTITIES; i++)
        if (entities[i].type == type)
            c++;
    return c;
}

int entity_spawn(int type, int32_t x, int32_t y, int32_t z)
{
    int slot = -1;
    for (int i = 0; i < MAX_ENTITIES; i++)
        if (entities[i].type == ET_NONE) {
            slot = i;
            break;
        }
    if (slot < 0) {
        // replace the oldest item entity
        int oldest = -1, age = -1;
        for (int i = 0; i < MAX_ENTITIES; i++)
            if (entities[i].type == ET_ITEM && entities[i].age > age) {
                age = entities[i].age;
                oldest = i;
            }
        if (oldest < 0)
            return -1;
        slot = oldest;
    }
    Entity *e = &entities[slot];
    memset(e, 0, sizeof(*e));
    e->type = type;
    e->body.x = world_wrap_fx(x);
    e->body.y = y;
    e->body.z = world_wrap_fx(z);
    e->body.hw = einfo[type].hw * 256;
    e->body.h = einfo[type].h * 256;
    e->health = einfo[type].health;
    e->yaw = rng_range(ANG_FULL);
    e->target_yaw = e->yaw;
    e->timer = rng_range(60);
    return slot;
}

void entity_spawn_item_vel(int32_t x, int32_t y, int32_t z, const ItemStack *s, int32_t vx, int32_t vy, int32_t vz)
{
    if (stack_empty(s))
        return;
    int i = entity_spawn(ET_ITEM, x, y, z);
    if (i < 0)
        return;
    Entity *e = &entities[i];
    e->item = *s;
    e->vx = vx;
    e->vy = vy;
    e->vz = vz;
    e->pickup_delay = 10;
    e->age = 0;
}

void entity_spawn_item(int32_t x, int32_t y, int32_t z, const ItemStack *s)
{
    entity_spawn_item_vel(x, y, z, s, (rng_fx() - 2048) / 24, FX(0.2), (rng_fx() - 2048) / 24);
}

void entity_shoot_arrow(int32_t x, int32_t y, int32_t z, int32_t dx, int32_t dy, int32_t dz, int32_t speed, bool from_player)
{
    int i = entity_spawn(ET_ARROW, x, y, z);
    if (i < 0)
        return;
    Entity *e = &entities[i];
    e->vx = fxmul(dx, speed);
    e->vy = fxmul(dy, speed);
    e->vz = fxmul(dz, speed);
    e->flags = from_player ? EF_FROM_PLAYER : 0;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static int32_t dist_xz(int32_t ax, int32_t az, int32_t bx, int32_t bz)
{
    int32_t dx = iabs(world_wrap_delta_fx(ax - bx)), dz = iabs(world_wrap_delta_fx(az - bz));
    // octagonal approximation of sqrt(dx^2 + dz^2)
    return dx > dz ? dx + dz * 3 / 8 : dz + dx * 3 / 8;
}

static int yaw_toward(int32_t dx, int32_t dz)
{
    // yaw such that forward (sin yaw, -cos yaw) points along (dx, dz)
    // atan2 with libnds: use a simple octant approximation
    int32_t ax = iabs(dx), az = iabs(dz);
    int a;
    if (ax == 0 && az == 0)
        return 0;
    // angle from north (-z) clockwise toward east (+x)
    if (ax > az)
        a = ANG_FULL / 4 - (int)((int64_t)az * (ANG_FULL / 8) / ax);
    else
        a = (int)((int64_t)ax * (ANG_FULL / 8) / az);
    // a is angle in first quadrant measured from north toward east
    if (dx >= 0 && dz <= 0)
        return a;
    if (dx >= 0 && dz > 0)
        return ANG_FULL / 2 - a;
    if (dx < 0 && dz > 0)
        return ANG_FULL / 2 + a;
    return (ANG_FULL - a) & (ANG_FULL - 1);
}

static int light_at(Entity *e)
{
    int l = world_light_raw(e->body.x >> 12, (e->body.y + 1024) >> 12, e->body.z >> 12);
    return l;
}

static bool line_of_sight(int32_t ax, int32_t ay, int32_t az, int32_t bx, int32_t by, int32_t bz)
{
    int32_t dx = world_wrap_delta_fx(bx - ax), dy = by - ay, dz = world_wrap_delta_fx(bz - az);
    for (int i = 1; i < 16; i++) {
        int32_t x = ax + dx * i / 16, y = ay + dy * i / 16, z = az + dz * i / 16;
        if (block_opaque(world_get(x >> 12, y >> 12, z >> 12)))
            return false;
    }
    return true;
}

static void drop_loot(Entity *e)
{
    ItemStack s = { 0 };
    int32_t x = e->body.x, y = e->body.y + 2048, z = e->body.z;
    bool burning = e->burn > 0;
    switch (e->type) {
        case ET_PIG:
            s.id = burning ? I_COOKED_PORK : I_RAW_PORK; s.count = 1 + rng_range(3); break;
        case ET_COW:
            s.id = burning ? I_STEAK : I_RAW_BEEF; s.count = 1 + rng_range(3);
            entity_spawn_item(x, y, z, &s);
            s.id = I_LEATHER; s.count = rng_range(3); break;
        case ET_SHEEP:
            if (!e->sheared) { s.id = B_WOOL_WHITE; s.count = 1; }
            break;
        case ET_CHICKEN:
            s.id = burning ? I_COOKED_CHICKEN : I_RAW_CHICKEN; s.count = 1;
            entity_spawn_item(x, y, z, &s);
            s.id = I_FEATHER; s.count = rng_range(3); break;
        case ET_ZOMBIE:
            s.id = I_ROTTEN_FLESH; s.count = rng_range(3); break;
        case ET_SKELETON:
            s.id = I_BONE; s.count = rng_range(3);
            entity_spawn_item(x, y, z, &s);
            s.id = I_ARROW; s.count = rng_range(3); break;
        case ET_SPIDER:
            s.id = I_STRING; s.count = rng_range(3); break;
        case ET_SHROOM:
            s.id = I_GUNPOWDER; s.count = rng_range(3); break;
        default:
            break;
    }
    if (s.count > 0)
        entity_spawn_item(x, y, z, &s);
}

static int mob_sound(int type)
{
    switch (type) {
        case ET_PIG: return SFX_PIG;
        case ET_COW: return SFX_COW;
        case ET_SHEEP: return SFX_SHEEP;
        case ET_CHICKEN: return SFX_CHICKEN;
        case ET_ZOMBIE: return SFX_ZOMBIE;
        case ET_SKELETON: return SFX_SKELETON;
        case ET_SPIDER: return SFX_SPIDER;
        default: return -1;
    }
}

void entity_damage(int idx, int dmg, int32_t kx, int32_t kz, bool by_player)
{
    Entity *e = &entities[idx];
    if (e->type == ET_NONE || e->type == ET_ITEM || e->type == ET_ARROW)
        return;
    if (e->type == ET_TNT)
        return;
    if (e->hurt > 0 || e->ai == 9)
        return;
    e->health -= dmg;
    e->hurt = 10;
    e->vx += fxmul(kx, FX(0.3));
    e->vz += fxmul(kz, FX(0.3));
    e->vy = FX(0.25);
    int snd = mob_sound(e->type);
    sound_play_at(snd >= 0 ? snd : SFX_HIT, e->body.x, e->body.y, e->body.z, 110, 120);
    sound_play_at(SFX_HIT, e->body.x, e->body.y, e->body.z, 90, 100);
    if (!einfo[e->type].hostile) {
        e->ai = 2; // flee
        e->timer = 50;
    } else if (by_player) {
        e->ai = 1;
    }
    if (e->health <= 0) {
        e->ai = 9;  // dying
        e->timer = 18;
        drop_loot(e);
    }
}

bool entity_interact(int idx, ItemStack *held)
{
    Entity *e = &entities[idx];
    if (e->type == ET_SHEEP && held->id == I_SHEARS && !e->sheared) {
        e->sheared = 1;
        ItemStack s = { .id = B_WOOL_WHITE, .count = 1 + rng_range(3) };
        entity_spawn_item(e->body.x, e->body.y + 4096, e->body.z, &s);
        sound_play_at(SFX_SHEEP, e->body.x, e->body.y, e->body.z, 100, 110);
        held->damage++;
        return true;
    }
    if (e->type == ET_COW && held->id == I_BUCKET) {
        if (held->count == 1)
            held->id = I_MILK_BUCKET;
        else {
            held->count--;
            player_give(I_MILK_BUCKET, 1, 0);
        }
        sound_play_at(SFX_COW, e->body.x, e->body.y, e->body.z, 100, 100);
        return true;
    }
    return false;
}

int entity_raycast(int32_t ex, int32_t ey, int32_t ez, int32_t dx, int32_t dy, int32_t dz, int32_t maxd, int32_t *out_t)
{
    int best = -1;
    int32_t best_t = maxd;
    for (int i = 0; i < MAX_ENTITIES; i++) {
        Entity *e = &entities[i];
        if (e->type == ET_NONE || e->type == ET_ITEM || e->type == ET_ARROW || e->ai == 9)
            continue;
        int32_t rx = world_wrap_delta_fx(e->body.x - ex), rz = world_wrap_delta_fx(e->body.z - ez);
        int32_t ry = e->body.y - ey;
        if (iabs(rx) > maxd + FX(2) || iabs(rz) > maxd + FX(2))
            continue;
        int32_t hw = e->body.hw + 410, h = e->body.h + 200;
        int32_t bmin[3] = { rx - hw, ry, rz - hw }, bmax[3] = { rx + hw, ry + h, rz + hw };
        int32_t d[3] = { dx, dy, dz };
        int32_t tmin = 0, tmax = maxd;
        bool ok = true;
        for (int a = 0; a < 3 && ok; a++) {
            if (d[a] == 0) {
                if (bmin[a] > 0 || bmax[a] < 0)
                    ok = false;
                continue;
            }
            int32_t t1 = (int32_t)(((int64_t)bmin[a] << 12) / d[a]);
            int32_t t2 = (int32_t)(((int64_t)bmax[a] << 12) / d[a]);
            if (t1 > t2) { int32_t t = t1; t1 = t2; t2 = t; }
            if (t1 > tmin) tmin = t1;
            if (t2 < tmax) tmax = t2;
            if (tmin > tmax)
                ok = false;
        }
        if (ok && tmin < best_t) {
            best_t = tmin;
            best = i;
        }
    }
    *out_t = best_t;
    return best;
}

// ---------------------------------------------------------------------------
// Explosions
// ---------------------------------------------------------------------------

void explosion(int32_t x, int32_t y, int32_t z, int radius)
{
    int cx = x >> 12, cy = y >> 12, cz = z >> 12;
    sound_play_at(SFX_EXPLODE, x, y, z, 127, 100);
    int r2 = radius * radius;
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dz = -radius; dz <= radius; dz++) {
            for (int dx = -radius; dx <= radius; dx++) {
                int d2 = dx * dx + dy * dy + dz * dz;
                if (d2 > r2 || (d2 > r2 - radius && rng_range(2)))
                    continue;
                int bx = cx + dx, by = cy + dy, bz = cz + dz;
                if (by <= 0 || by >= WORLD_H)
                    continue;
                int id = world_get(bx, by, bz);
                if (id == B_AIR || id == B_BEDROCK || id == B_OBSIDIAN || block_liquid(id))
                    continue;
                if (id == B_TNT) {
                    world_set(bx, by, bz, B_AIR);
                    int e = entity_spawn(ET_TNT, (bx << 12) + 2048, by << 12, (bz << 12) + 2048);
                    if (e >= 0)
                        entities[e].fuse = 10 + rng_range(20);
                    continue;
                }
                sim_break_block(bx, by, bz, rng_range(4) == 0, -1);
            }
        }
    }
    for (int i = 0; i < 8; i++)
        particles_block(cx + rng_range(3) - 1, cy + rng_range(3) - 1, cz + rng_range(3) - 1, B_COBBLESTONE);
    // damage
    int32_t pr = FX(radius * 2);
    int32_t pd = dist_xz(player.body.x, player.body.z, x, z) + iabs(player.body.y + FX(0.9) - y);
    if (pd < pr) {
        int dmg = (int)(((int64_t)(pr - pd) * radius * 5) / pr) + 1;
        player_damage(dmg, DMG_EXPLOSION);
        int32_t kx = world_wrap_delta_fx(player.body.x - x), kz = world_wrap_delta_fx(player.body.z - z);
        player.vx += kx / 8;
        player.vz += kz / 8;
        player.vy += FX(0.4);
    }
    for (int i = 0; i < MAX_ENTITIES; i++) {
        Entity *e = &entities[i];
        if (e->type == ET_NONE || e->type == ET_TNT)
            continue;
        int32_t d = dist_xz(e->body.x, e->body.z, x, z) + iabs(e->body.y - y);
        if (d < pr) {
            if (e->type == ET_ITEM || e->type == ET_ARROW) {
                if (rng_range(3) == 0)
                    e->type = ET_NONE;
                continue;
            }
            int dmg = (int)(((int64_t)(pr - d) * radius * 5) / pr) + 1;
            entity_damage(i, dmg, world_wrap_delta_fx(e->body.x - x) / 2, world_wrap_delta_fx(e->body.z - z) / 2, false);
        }
    }
}

// ---------------------------------------------------------------------------
// AI & physics
// ---------------------------------------------------------------------------

static void particles_tick(void);

static void walk(Entity *e, int32_t accel)
{
    int32_t s = sinLerp(e->yaw), c = cosLerp(e->yaw);
    e->vx += fxmul(s, accel);
    e->vz -= fxmul(c, accel);
}

static void turn_toward(Entity *e, int target, int rate)
{
    int d = (target - e->yaw) & (ANG_FULL - 1);
    if (d > ANG_FULL / 2)
        d -= ANG_FULL;
    if (d > rate) d = rate;
    if (d < -rate) d = -rate;
    e->yaw = (e->yaw + d) & (ANG_FULL - 1);
}

static void mob_physics(Entity *e, bool can_fly_slow)
{
    bool water = body_in_water(&e->body) || body_in_lava(&e->body);
    e->in_water = water;
    if (water) {
        e->vy += FX(0.04);   // swim up
        if (e->vy > FX(0.1)) e->vy = FX(0.1);
    }
    int32_t oldy = e->body.y;
    body_move(&e->body, e->vx, e->vy, e->vz, FX(0.5));
    bool ground = e->body.on_ground;
    e->on_ground = ground;
    if (e->body.hit_x) e->vx = 0;
    if (e->body.hit_z) e->vz = 0;
    if (e->body.hit_y) e->vy = 0;
    // fall damage for mobs
    if (ground) {
        if (e->fall > FX(3) && !water && e->type != ET_CHICKEN && e->type != ET_ITEM) {
            int idx = e - entities;
            entity_damage(idx, (e->fall - FX(3)) >> 12, 0, 0, false);
        }
        e->fall = 0;
    } else if (e->body.y < oldy) {
        e->fall += oldy - e->body.y;
    }
    int32_t slip = ground ? FX(0.546) : FX(0.91);
    if (water)
        slip = FX(0.8);
    e->vy = fxmul(e->vy - (water ? FX(0.02) : FX(0.08)), FX(0.98));
    if (can_fly_slow && e->vy < -FX(0.06))
        e->vy = -FX(0.06);
    e->vx = fxmul(e->vx, slip);
    e->vz = fxmul(e->vz, slip);
    if (e->body.y < -FX(32))
        e->type = ET_NONE;
}

static void walk_anim(Entity *e)
{
    int32_t sp = iabs(e->vx) + iabs(e->vz);
    e->walk_phase += sp * 5;
    int32_t target = sp > 200 ? 4096 : 0;
    e->walk_amt += (target - e->walk_amt) / 4;
}

static void passive_ai(Entity *e)
{
    int32_t speed = einfo[e->type].speed;
    if (e->ai == 2) {
        // flee: a brisk trot, not a sprint, changing direction now and then
        walk(e, speed * 13 / 10);
        if (e->timer % 30 == 0)
            e->target_yaw = rng_range(ANG_FULL);
        turn_toward(e, e->target_yaw, 500);
        if (--e->timer <= 0)
            e->ai = 0;
    } else {
        if (--e->timer <= 0) {
            if (e->ai == 3) {
                e->ai = 0;
                e->timer = 40 + rng_range(120);
            } else {
                e->ai = 3; // wander
                e->target_yaw = rng_range(ANG_FULL);
                e->timer = 30 + rng_range(60);
            }
            if (rng_range(6) == 0) {
                int snd = mob_sound(e->type);
                if (snd >= 0)
                    sound_play_at(snd, e->body.x, e->body.y, e->body.z, 70, 90 + rng_range(30));
            }
        }
        if (e->ai == 3) {
            turn_toward(e, e->target_yaw, 300);
            walk(e, speed);
        }
    }
    if ((e->body.hit_x || e->body.hit_z) && e->on_ground)
        e->vy = FX(0.42);
    if (e->type == ET_CHICKEN && (e->age % 6000) == 5999) {
        ItemStack s = { .id = I_EGG, .count = 1 };
        entity_spawn_item(e->body.x, e->body.y, e->body.z, &s);
    }
}

static void hostile_ai(Entity *e, int idx)
{
    int32_t speed = einfo[e->type].speed;
    Player *p = &player;
    bool target_ok = !p->dead && p->mode == MODE_SURVIVAL && !settings.peaceful;
    int32_t dx = world_wrap_delta_fx(p->body.x - e->body.x);
    int32_t dz = world_wrap_delta_fx(p->body.z - e->body.z);
    int32_t dy = p->body.y - e->body.y;
    int32_t dist = dist_xz(p->body.x, p->body.z, e->body.x, e->body.z);
    int light = light_at(e);
    int sky = light >> 4;
    bool day = sky_daylight > 20;

    // Spiders are calm in daylight unless provoked
    if (e->type == ET_SPIDER && day && sky >= 12 && e->ai != 1)
        target_ok = false;

    bool chasing = target_ok && dist < FX(16) && iabs(dy) < FX(8);
    if (chasing && e->type != ET_SPIDER && dist > FX(4)) {
        int32_t ey = e->body.y + e->body.h - 410;
        chasing = line_of_sight(e->body.x, ey, e->body.z, p->body.x, p->body.y + FX(1.5), p->body.z) || e->ai == 1;
    }
    if (!chasing) {
        passive_ai(e);
        if (e->type == ET_SHROOM && e->fuse > 0)
            e->fuse--;
        return;
    }
    e->ai = e->ai == 1 ? 1 : 0;
    int ty = yaw_toward(dx, dz);
    turn_toward(e, ty, 900);
    switch (e->type) {
        case ET_ZOMBIE:
            walk(e, speed);
            if (dist < FX(1.3) && iabs(dy) < FX(2) && e->attack_cd == 0) {
                player_damage(3, DMG_MOB);
                player.vx += fxmul(sinLerp(e->yaw), FX(0.3));
                player.vz -= fxmul(cosLerp(e->yaw), FX(0.3));
                e->attack_cd = 20;
            }
            if ((rng_next() & 511) == 0)
                sound_play_at(SFX_ZOMBIE, e->body.x, e->body.y, e->body.z, 80, 100);
            break;
        case ET_SKELETON:
            if (dist > FX(9))
                walk(e, speed);
            else if (dist < FX(5))
                walk(e, -speed);
            if (e->attack_cd == 0 && dist < FX(15)) {
                int32_t ex = e->body.x, ey = e->body.y + FX(1.5), ez = e->body.z;
                int32_t tx = dx, ty2 = dy + FX(1.2) - FX(1.5) + dist / 10, tz = dz;
                // normalize
                int32_t len = dist_xz(0, 0, tx, tz) + iabs(ty2) / 2;
                if (len < 4096) len = 4096;
                int32_t nx = fxdiv(tx, len), ny = fxdiv(ty2, len), nz = fxdiv(tz, len);
                entity_shoot_arrow(ex + nx / 2, ey, ez + nz / 2, nx, ny, nz, FX(1.4), false);
                sound_play_at(SFX_BOW, e->body.x, e->body.y, e->body.z, 90, 110);
                e->attack_cd = 40 + rng_range(20);
            }
            break;
        case ET_SPIDER:
            walk(e, speed);
            if (dist < FX(3) && dist > FX(1.5) && e->on_ground && rng_range(10) == 0) {
                e->vy = FX(0.4);
                e->vx += fxmul(sinLerp(e->yaw), FX(0.3));
                e->vz -= fxmul(cosLerp(e->yaw), FX(0.3));
            }
            if ((e->body.hit_x || e->body.hit_z))
                e->vy = FX(0.2);  // climb walls
            if (dist < FX(1.5) && iabs(dy) < FX(2) && e->attack_cd == 0) {
                player_damage(2, DMG_MOB);
                e->attack_cd = 20;
            }
            break;
        case ET_SHROOM:
            if (dist < FX(2.8)) {
                if (e->fuse == 0)
                    sound_play_at(SFX_FUSE, e->body.x, e->body.y, e->body.z, 110, 100);
                e->fuse++;
                if (e->fuse >= 30) {
                    e->type = ET_NONE;
                    explosion(e->body.x, e->body.y + FX(0.5), e->body.z, 3);
                    return;
                }
            } else {
                if (e->fuse > 0)
                    e->fuse--;
                walk(e, speed);
            }
            break;
    }
    if ((e->body.hit_x || e->body.hit_z) && e->on_ground && e->type != ET_SPIDER)
        e->vy = FX(0.42);
    (void)idx;
}

static void item_tick(Entity *e, int idx)
{
    if (e->pickup_delay > 0)
        e->pickup_delay--;
    bool water = body_in_water(&e->body);
    if (water)
        e->vy = imin(e->vy + FX(0.06), FX(0.08));
    // magnet toward player
    int32_t dx = world_wrap_delta_fx(player.body.x - e->body.x);
    int32_t dz = world_wrap_delta_fx(player.body.z - e->body.z);
    int32_t dy = player.body.y + FX(0.5) - e->body.y;
    if (!player.dead && e->pickup_delay == 0 && iabs(dx) < FX(2) && iabs(dz) < FX(2) && iabs(dy) < FX(2)) {
        e->vx += dx / 16;
        e->vz += dz / 16;
        e->vy += dy / 16;
        if (iabs(dx) < FX(0.9) && iabs(dz) < FX(0.9) && iabs(dy) < FX(1.4)) {
            int left = inv_add(player.inv, INV_SIZE, e->item.id, e->item.count, e->item.damage);
            if (left < e->item.count) {
                sound_play(SFX_POP, 70, 64, 80 + rng_range(60));
                if (left == 0) {
                    e->type = ET_NONE;
                    return;
                }
                e->item.count = left;
            }
        }
    }
    body_move(&e->body, e->vx, e->vy, e->vz, 0);
    if (e->body.hit_y) e->vy = 0;
    e->vy = fxmul(e->vy - FX(0.04), FX(0.98));
    int32_t slip = e->body.on_ground ? FX(0.55) : FX(0.98);
    e->vx = fxmul(e->vx, slip);
    e->vz = fxmul(e->vz, slip);
    // merge with neighbors
    if ((e->age & 15) == (idx & 15)) {
        for (int j = 0; j < MAX_ENTITIES; j++) {
            Entity *o = &entities[j];
            if (j == idx || o->type != ET_ITEM || o->item.id != e->item.id || o->item.damage != e->item.damage)
                continue;
            if (iabs(world_wrap_delta_fx(o->body.x - e->body.x)) < FX(0.8) && iabs(o->body.y - e->body.y) < FX(0.8) &&
                iabs(world_wrap_delta_fx(o->body.z - e->body.z)) < FX(0.8)) {
                int max = item_max_stack(e->item.id);
                if (e->item.count + o->item.count <= max) {
                    e->item.count += o->item.count;
                    o->type = ET_NONE;
                }
            }
        }
    }
    if (e->age > 6000 || is_lava(world_get(e->body.x >> 12, e->body.y >> 12, e->body.z >> 12)))
        e->type = ET_NONE;
}

static void arrow_tick(Entity *e, int idx)
{
    if (e->flags & EF_STUCK) {
        if (e->age > 1200)
            e->type = ET_NONE;
        if (e->flags & EF_FROM_PLAYER) {
            int32_t dx = world_wrap_delta_fx(player.body.x - e->body.x);
            int32_t dz = world_wrap_delta_fx(player.body.z - e->body.z);
            int32_t dy = player.body.y + FX(0.5) - e->body.y;
            if (iabs(dx) < FX(1) && iabs(dz) < FX(1) && iabs(dy) < FX(1.5)) {
                if (inv_add(player.inv, INV_SIZE, I_ARROW, 1, 0) == 0) {
                    sound_play(SFX_POP, 70, 64, 100);
                    e->type = ET_NONE;
                }
            }
        }
        return;
    }
    int32_t ox = e->body.x, oy = e->body.y, oz = e->body.z;
    int32_t speed = iabs(e->vx) + iabs(e->vy) + iabs(e->vz);
    // entity hits along the path
    int32_t len = speed ? speed : 1;
    int32_t dxn = fxdiv(e->vx, len), dyn = fxdiv(e->vy, len), dzn = fxdiv(e->vz, len);
    if (e->age > 1) {
        if (!(e->flags & EF_FROM_PLAYER) && !player.dead) {
            int32_t px = world_wrap_delta_fx(player.body.x - ox), pz = world_wrap_delta_fx(player.body.z - oz);
            int32_t py = player.body.y - oy;
            for (int s = 0; s <= 4; s++) {
                int32_t tx = e->vx * s / 4, ty = e->vy * s / 4, tz = e->vz * s / 4;
                if (iabs(px - tx) < FX(0.5) && iabs(pz - tz) < FX(0.5) && ty > py - 200 && ty < py + FX(1.9)) {
                    player_damage(2 + (speed >> 12), DMG_ARROW);
                    e->type = ET_NONE;
                    return;
                }
            }
        }
        int32_t t;
        int hit = entity_raycast(ox, oy, oz, dxn, dyn, dzn, len, &t);
        if (hit >= 0 && hit != idx) {
            entity_damage(hit, 2 + (speed >> 12) * 2, e->vx, e->vz, (e->flags & EF_FROM_PLAYER) != 0);
            e->type = ET_NONE;
            return;
        }
    }
    body_move(&e->body, e->vx, e->vy, e->vz, 0);
    if (e->body.hit_x || e->body.hit_y || e->body.hit_z) {
        e->flags |= EF_STUCK;
        e->age = 0;
        sound_play_at(SFX_HIT, e->body.x, e->body.y, e->body.z, 70, 150);
        return;
    }
    e->vy -= FX(0.05);
    e->vx = fxmul(e->vx, FX(0.99));
    e->vy = fxmul(e->vy, FX(0.99));
    e->vz = fxmul(e->vz, FX(0.99));
    e->yaw = yaw_toward(e->vx, e->vz);
}

static void tnt_tick(Entity *e)
{
    body_move(&e->body, e->vx, e->vy, e->vz, 0);
    if (e->body.hit_y) e->vy = 0;
    e->vy = fxmul(e->vy - FX(0.04), FX(0.98));
    if (--e->fuse <= 0) {
        e->type = ET_NONE;
        explosion(e->body.x, e->body.y + FX(0.5), e->body.z, 4);
    }
}

// ---------------------------------------------------------------------------
// Spawning
// ---------------------------------------------------------------------------

static bool find_spawn_y(int x, int z, int ymin, int ymax, int *out_y)
{
    for (int tries = 0; tries < 6; tries++) {
        int y = ymin + rng_range(imax(1, ymax - ymin));
        // move down to the floor
        while (y > 1 && !block_solid(world_get(x, y - 1, z)))
            y--;
        if (block_solid(world_get(x, y, z)) || block_solid(world_get(x, y + 1, z)))
            continue;
        if (block_liquid(world_get(x, y, z)) || block_liquid(world_get(x, y - 1, z)))
            continue;
        if (!block_opaque(world_get(x, y - 1, z)))
            continue;
        *out_y = y;
        return true;
    }
    return false;
}

static void spawn_passive_group(int x, int z)
{
    int y = WORLD_H - 2;
    while (y > 0 && world_get(x, y, z) == B_AIR)
        y--;
    int g = world_get(x, y, z);
    if (g != B_GRASS && g != B_SNOWY_GRASS)
        return;
    static const uint8_t kinds[4] = { ET_PIG, ET_COW, ET_SHEEP, ET_CHICKEN };
    int type = kinds[rng_range(4)];
    int n = 2 + rng_range(3);
    for (int i = 0; i < n; i++) {
        int sx = x + rng_range(5) - 2, sz = z + rng_range(5) - 2;
        int sy = WORLD_H - 2;
        while (sy > 0 && world_get(sx, sy, sz) == B_AIR)
            sy--;
        int gg = world_get(sx, sy, sz);
        if ((gg == B_GRASS || gg == B_SNOWY_GRASS) && world_get(sx, sy + 1, sz) == B_AIR && world_get(sx, sy + 2, sz) == B_AIR)
            entity_spawn(type, (sx << 12) + 2048, (sy + 1) << 12, (sz << 12) + 2048);
    }
}

void entities_spawn_initial(void)
{
    int groups = world.chunks * world.chunks / 3;
    for (int i = 0; i < groups; i++)
        spawn_passive_group(rng_range(world.size), rng_range(world.size));
}

static void try_spawn(void)
{
    int hostiles = 0, passives = 0;
    for (int i = 0; i < MAX_ENTITIES; i++) {
        int t = entities[i].type;
        if (t >= ET_ZOMBIE && t <= ET_SHROOM)
            hostiles++;
        else if (t >= ET_PIG && t <= ET_CHICKEN)
            passives++;
    }
    int px = player.body.x >> 12, pz = player.body.z >> 12;
    // Hostile mobs in the dark
    if (player.mode == MODE_SURVIVAL && !settings.peaceful && hostiles < 8) {
        int ang = rng_range(ANG_FULL);
        int d = 18 + rng_range(20);
        int x = px + (sinLerp(ang) * d >> 12), z = pz + (cosLerp(ang) * d >> 12);
        int surface = world_height(x, z);
        int y;
        bool cave = rng_range(2) == 0;
        if (find_spawn_y(x, z, cave ? 4 : surface, cave ? imax(5, surface - 4) : surface + 3, &y)) {
            int l = world_light_raw(x, y, z);
            int sky = (l >> 4) * sky_daylight / 31;
            int blk = l & 15;
            if (sky <= 7 && blk <= 7) {
                int r = rng_range(100);
                int type = r < 35 ? ET_ZOMBIE : r < 65 ? ET_SKELETON : r < 85 ? ET_SPIDER : ET_SHROOM;
                if (type == ET_SPIDER && (block_solid(world_get(x + 1, y, z)) || block_solid(world_get(x - 1, y, z))))
                    type = ET_ZOMBIE;
                entity_spawn(type, (x << 12) + 2048, y << 12, (z << 12) + 2048);
            }
        }
    }
    // Animals slowly come back
    if (passives < 10 && rng_range(10) == 0) {
        int ang = rng_range(ANG_FULL);
        int d = 28 + rng_range(20);
        spawn_passive_group(px + (sinLerp(ang) * d >> 12), pz + (cosLerp(ang) * d >> 12));
    }
}

void entities_tick(void)
{
    for (int i = 0; i < MAX_ENTITIES; i++) {
        Entity *e = &entities[i];
        if (e->type == ET_NONE)
            continue;
        e->age++;
        if (e->hurt > 0)
            e->hurt--;
        if (e->attack_cd > 0)
            e->attack_cd--;
        int32_t far = dist_xz(e->body.x, e->body.z, player.body.x, player.body.z);
        switch (e->type) {
            case ET_ITEM:
                item_tick(e, i);
                break;
            case ET_ARROW:
                arrow_tick(e, i);
                break;
            case ET_TNT:
                tnt_tick(e);
                break;
            default:
                if (e->ai == 9) {
                    // dying animation
                    if (--e->timer <= 0) {
                        e->type = ET_NONE;
                        particles_block(e->body.x >> 12, (e->body.y >> 12), e->body.z >> 12, B_WOOL_WHITE + 8);
                    }
                    mob_physics(e, false);
                    break;
                }
                // Despawn far monsters
                if (einfo[e->type].hostile && (far > FX(64) || (far > FX(32) && rng_range(600) == 0))) {
                    e->type = ET_NONE;
                    break;
                }
                // Freeze very distant mobs (cheap)
                if (far > FX(48))
                    break;
                if (einfo[e->type].hostile)
                    hostile_ai(e, i);
                else
                    passive_ai(e);
                if (e->type == ET_NONE)
                    break;
                // Daylight burning for undead
                if ((e->type == ET_ZOMBIE || e->type == ET_SKELETON) && sky_daylight > 24 && !e->in_water) {
                    int l = light_at(e);
                    if ((l >> 4) >= 15 && world_height(e->body.x >> 12, e->body.z >> 12) <= (e->body.y >> 12) + 1)
                        e->burn = 20;
                }
                if (e->burn > 0) {
                    e->burn--;
                    if ((e->age % 20) == 0)
                        entity_damage(i, 1, 0, 0, false);
                    if (e->in_water)
                        e->burn = 0;
                }
                if (body_in_lava(&e->body) && (e->age % 10) == 0) {
                    e->burn = 100;
                    entity_damage(i, 4, 0, 0, false);
                }
                mob_physics(e, e->type == ET_CHICKEN);
                walk_anim(e);
                break;
        }
    }
    if ((world.ticks % 40) == 0)
        try_spawn();
    particles_tick();
}

// ---------------------------------------------------------------------------
// Particles
// ---------------------------------------------------------------------------

#define MAX_PARTICLES 48
typedef struct {
    int32_t x, y, z, vx, vy, vz;
    int16_t life;
    uint8_t tex;
    uint8_t u, v;
} Particle;
static Particle particles[MAX_PARTICLES];

void particles_clear(void)
{
    memset(particles, 0, sizeof(particles));
}

void particles_block(int x, int y, int z, int block_id)
{
    if (block_id == B_AIR)
        return;
    int tex = blocks[block_id].tex[FACE_N];
    int n = 0;
    for (int i = 0; i < MAX_PARTICLES && n < 8; i++) {
        Particle *p = &particles[i];
        if (p->life > 0)
            continue;
        p->x = (x << 12) + 1024 + (rng_fx() >> 1);
        p->y = (y << 12) + 1024 + (rng_fx() >> 1);
        p->z = (z << 12) + 1024 + (rng_fx() >> 1);
        p->vx = (rng_fx() - 2048) / 20;
        p->vy = (rng_fx()) / 16;
        p->vz = (rng_fx() - 2048) / 20;
        p->life = 10 + rng_range(10);
        p->tex = tex;
        p->u = rng_range(12);
        p->v = rng_range(12);
        n++;
    }
}

static void particles_tick(void)
{
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &particles[i];
        if (p->life <= 0)
            continue;
        p->life--;
        p->x += p->vx;
        p->y += p->vy;
        p->z += p->vz;
        p->vy -= FX(0.04);
        if (block_solid(world_get(p->x >> 12, p->y >> 12, p->z >> 12))) {
            p->vy = 0;
            p->vx = p->vz = 0;
            p->y = ((p->y >> 12) + 1) << 12;
        }
    }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

// Box model part (units: 1/16 block, origin at feet center, model faces -Z)
typedef struct {
    int8_t x0, y0, z0, x1, y1, z1;
    int8_t py, pz;      // pivot (y, z) for X rotation
    uint8_t anim;       // 0 static, 1 leg A, 2 leg B, 3 head, 4 arms forward, 5 wing
    uint8_t tex;        // skin texture
    uint8_t face_tex;   // texture for the -Z face (0 = same)
    uint8_t layer;      // 1 = wool layer (sheep only when not sheared)
} Part;

#define P(x0, y0, z0, x1, y1, z1, py, pz, anim, tex, ftex) { x0, y0, z0, x1, y1, z1, py, pz, anim, tex, ftex, 0 }

static const Part m_pig[] = {
    P(-5, 6, -8, 5, 14, 8, 0, 0, 0, TEX_PIG_SKIN, 0),
    P(-4, 8, -14, 4, 16, -6, 12, -6, 3, TEX_PIG_SKIN, TEX_PIG_FACE),
    P(-5, 0, -7, -1, 6, -3, 6, -5, 1, TEX_PIG_SKIN, 0),
    P(1, 0, -7, 5, 6, -3, 6, -5, 2, TEX_PIG_SKIN, 0),
    P(-5, 0, 3, -1, 6, 7, 6, 5, 2, TEX_PIG_SKIN, 0),
    P(1, 0, 3, 5, 6, 7, 6, 5, 1, TEX_PIG_SKIN, 0),
};
static const Part m_cow[] = {
    P(-6, 12, -9, 6, 22, 9, 0, 0, 0, TEX_COW_SKIN, 0),
    P(-4, 16, -15, 4, 24, -9, 20, -9, 3, TEX_COW_SKIN, TEX_COW_FACE),
    P(-6, 0, -8, -2, 12, -4, 12, -6, 1, TEX_COW_SKIN, 0),
    P(2, 0, -8, 6, 12, -4, 12, -6, 2, TEX_COW_SKIN, 0),
    P(-6, 0, 4, -2, 12, 8, 12, 6, 2, TEX_COW_SKIN, 0),
    P(2, 0, 4, 6, 12, 8, 12, 6, 1, TEX_COW_SKIN, 0),
};
static const Part m_sheep[] = {
    P(-4, 12, -8, 4, 18, 8, 0, 0, 0, TEX_SHEEP_SKIN, 0),
    { -6, 11, -9, 6, 20, 9, 0, 0, 0, TEX_SHEEP_WOOL, 0, 1 },
    P(-3, 15, -14, 3, 21, -7, 18, -7, 3, TEX_SHEEP_SKIN, TEX_SHEEP_FACE),
    P(-5, 0, -7, -1, 12, -3, 12, -5, 1, TEX_SHEEP_SKIN, 0),
    P(1, 0, -7, 5, 12, -3, 12, -5, 2, TEX_SHEEP_SKIN, 0),
    P(-5, 0, 3, -1, 12, 7, 12, 5, 2, TEX_SHEEP_SKIN, 0),
    P(1, 0, 3, 5, 12, 7, 12, 5, 1, TEX_SHEEP_SKIN, 0),
};
static const Part m_chicken[] = {
    P(-3, 4, -4, 3, 10, 4, 0, 0, 0, TEX_CHICKEN_FEATHERS, 0),
    P(-2, 8, -7, 2, 14, -3, 10, -4, 3, TEX_CHICKEN_FEATHERS, TEX_CHICKEN_FACE),
    P(-4, 6, -3, -3, 10, 3, 10, 0, 5, TEX_CHICKEN_FEATHERS, 0),
    P(3, 6, -3, 4, 10, 3, 10, 0, 5, TEX_CHICKEN_FEATHERS, 0),
    P(-2, 0, -1, -1, 5, 0, 5, 0, 1, TEX_CHICKEN_FACE, 0),
    P(1, 0, -1, 2, 5, 0, 5, 0, 2, TEX_CHICKEN_FACE, 0),
};
static const Part m_zombie[] = {
    P(-4, 0, -2, 0, 12, 2, 12, 0, 1, TEX_ZOMBIE_PANTS, 0),
    P(0, 0, -2, 4, 12, 2, 12, 0, 2, TEX_ZOMBIE_PANTS, 0),
    P(-4, 12, -2, 4, 24, 2, 0, 0, 0, TEX_ZOMBIE_SHIRT, 0),
    P(-8, 12, -2, -4, 24, 2, 22, 0, 4, TEX_ZOMBIE_SKIN, 0),
    P(4, 12, -2, 8, 24, 2, 22, 0, 4, TEX_ZOMBIE_SKIN, 0),
    P(-4, 24, -4, 4, 32, 4, 24, 0, 3, TEX_ZOMBIE_SKIN, TEX_ZOMBIE_FACE),
};
static const Part m_skeleton[] = {
    P(-3, 0, -1, -1, 12, 1, 12, 0, 1, TEX_SKELETON_BONE, 0),
    P(1, 0, -1, 3, 12, 1, 12, 0, 2, TEX_SKELETON_BONE, 0),
    P(-4, 12, -2, 4, 24, 2, 0, 0, 0, TEX_SKELETON_BONE, 0),
    P(-6, 12, -1, -4, 24, 1, 22, 0, 4, TEX_SKELETON_BONE, 0),
    P(4, 12, -1, 6, 24, 1, 22, 0, 4, TEX_SKELETON_BONE, 0),
    P(-4, 24, -4, 4, 32, 4, 24, 0, 3, TEX_SKELETON_BONE, TEX_SKELETON_FACE),
};
static const Part m_spider[] = {
    P(-5, 4, 2, 5, 12, 14, 0, 0, 0, TEX_SPIDER_BODY, 0),
    P(-3, 5, -4, 3, 11, 2, 0, 0, 0, TEX_SPIDER_BODY, 0),
    P(-4, 4, -12, 4, 12, -4, 8, -4, 3, TEX_SPIDER_BODY, TEX_SPIDER_FACE),
    P(-16, 2, -4, -3, 4, -2, 3, -3, 1, TEX_SPIDER_BODY, 0),
    P(3, 2, -4, 16, 4, -2, 3, -3, 2, TEX_SPIDER_BODY, 0),
    P(-16, 2, -1, -3, 4, 1, 3, 0, 2, TEX_SPIDER_BODY, 0),
    P(3, 2, -1, 16, 4, 1, 3, 0, 1, TEX_SPIDER_BODY, 0),
    P(-16, 2, 2, -3, 4, 4, 3, 3, 1, TEX_SPIDER_BODY, 0),
    P(3, 2, 2, 16, 4, 4, 3, 3, 2, TEX_SPIDER_BODY, 0),
};
static const Part m_shroom[] = {
    P(-3, 4, -3, 3, 18, 3, 0, 0, 0, TEX_SHROOM_STEM, TEX_SHROOM_FACE),
    P(-7, 18, -7, 7, 25, 7, 0, 0, 0, TEX_SHROOM_CAP, 0),
    P(-3, 0, -4, 0, 4, 2, 4, -1, 1, TEX_SHROOM_STEM, 0),
    P(0, 0, -4, 3, 4, 2, 4, -1, 2, TEX_SHROOM_STEM, 0),
};

typedef struct {
    const Part *parts;
    int n;
} Model;

#define MODEL(a) { a, sizeof(a) / sizeof(a[0]) }
static const Model models[ET_COUNT] = {
    [ET_PIG] = MODEL(m_pig), [ET_COW] = MODEL(m_cow), [ET_SHEEP] = MODEL(m_sheep),
    [ET_CHICKEN] = MODEL(m_chicken), [ET_ZOMBIE] = MODEL(m_zombie), [ET_SKELETON] = MODEL(m_skeleton),
    [ET_SPIDER] = MODEL(m_spider), [ET_SHROOM] = MODEL(m_shroom),
};

static int cur_light;
static bool cur_hurt;

static void set_material(int dir)
{
    uint16_t amb = light_ambient[dir][cur_light >> 4];
    uint16_t emi = light_emission[dir][cur_light & 15];
    if (cur_hurt)
        emi |= RGB15(20, 0, 0);
    GFX_DIFFUSE_AMBIENT = (uint32_t)amb << 16;
    GFX_SPECULAR_EMISSION = (uint32_t)emi << 16;
    GFX_NORMAL = 0;
}

// Box with px units (scaled by 1/8 units per px via matrix: raw = px * 512)
static void tex_box(int x0, int y0, int z0, int x1, int y1, int z1, int tex, int face_tex)
{
#define VX(x, y, z) glVertex3v16((x) * 512, (y) * 512, (z) * 512)
    int ft = face_tex ? face_tex : tex;
    // -Z (front)
    GFX_TEX_FORMAT = gx_texparam16(ft, TEXGEN_NONE);
    glBegin(GL_QUADS);
    set_material(FACE_N);
    glTexCoord2t16(256, 256); VX(x0, y0, z0);
    glTexCoord2t16(256, 0); VX(x0, y1, z0);
    glTexCoord2t16(0, 0); VX(x1, y1, z0);
    glTexCoord2t16(0, 256); VX(x1, y0, z0);
    glEnd();
    GFX_TEX_FORMAT = gx_texparam16(tex, TEXGEN_NONE);
    // +Z
    glBegin(GL_QUADS);
    set_material(FACE_S);
    glTexCoord2t16(0, 256); VX(x0, y0, z1);
    glTexCoord2t16(256, 256); VX(x1, y0, z1);
    glTexCoord2t16(256, 0); VX(x1, y1, z1);
    glTexCoord2t16(0, 0); VX(x0, y1, z1);
    glEnd();
    // -X
    glBegin(GL_QUADS);
    set_material(FACE_W);
    glTexCoord2t16(0, 256); VX(x0, y0, z0);
    glTexCoord2t16(256, 256); VX(x0, y0, z1);
    glTexCoord2t16(256, 0); VX(x0, y1, z1);
    glTexCoord2t16(0, 0); VX(x0, y1, z0);
    glEnd();
    // +X
    glBegin(GL_QUADS);
    set_material(FACE_E);
    glTexCoord2t16(256, 256); VX(x1, y0, z0);
    glTexCoord2t16(256, 0); VX(x1, y1, z0);
    glTexCoord2t16(0, 0); VX(x1, y1, z1);
    glTexCoord2t16(0, 256); VX(x1, y0, z1);
    glEnd();
    // top
    glBegin(GL_QUADS);
    set_material(FACE_U);
    glTexCoord2t16(0, 0); VX(x0, y1, z0);
    glTexCoord2t16(0, 256); VX(x0, y1, z1);
    glTexCoord2t16(256, 256); VX(x1, y1, z1);
    glTexCoord2t16(256, 0); VX(x1, y1, z0);
    glEnd();
    // bottom
    glBegin(GL_QUADS);
    set_material(FACE_D);
    glTexCoord2t16(0, 0); VX(x0, y0, z0);
    glTexCoord2t16(256, 0); VX(x1, y0, z0);
    glTexCoord2t16(256, 256); VX(x1, y0, z1);
    glTexCoord2t16(0, 256); VX(x0, y0, z1);
    glEnd();
#undef VX
}

static bool on_screen(int32_t rx, int32_t ry, int32_t rz)
{
    // quick reject behind the camera
    int32_t fx, fy, fz;
    camera_forward(&fx, &fy, &fz);
    int32_t d = fxmul(rx, fx) + fxmul(ry, fy) + fxmul(rz, fz);
    if (d < -FX(2))
        return false;
    int32_t md = (render_distance * 16) << 12;
    return iabs(rx) < md && iabs(rz) < md;
}

static void render_mob(Entity *e)
{
    const Model *m = &models[e->type];
    if (!m->parts)
        return;
    int32_t rx, ry, rz;
    render_rel(e->body.x, e->body.y, e->body.z, &rx, &ry, &rz);
    if (!on_screen(rx, ry + FX(1), rz))
        return;
    cur_light = light_at(e);
    cur_hurt = e->hurt > 0 || e->burn > 0 || (e->type == ET_SHROOM && e->fuse > 0 && (e->fuse & 4));
    glPushMatrix();
    glTranslatef32(rx >> 2, ry >> 2, rz >> 2);
    glRotateYi(-e->yaw);
    if (e->ai == 9) {
        // fall over sideways while dying
        glRotateZi((18 - e->timer) * (ANG_FULL / 4) / 18);
    }
    glScalef32(512, 512, 512);
    int32_t swing = fxmul(sinLerp(e->walk_phase), e->walk_amt);
    int leg = (swing * (ANG_FULL / 10)) >> 12;
    for (int i = 0; i < m->n; i++) {
        const Part *p = &m->parts[i];
        if (p->layer == 1 && e->sheared)
            continue;
        int ang = 0;
        switch (p->anim) {
            case 1: ang = leg; break;
            case 2: ang = -leg; break;
            case 4: ang = (e->type == ET_ZOMBIE ? -ANG_FULL / 4 : -ANG_FULL / 5) + leg / 3; break;
            case 5: ang = 0; break;
            default: break;
        }
        if (ang) {
            glPushMatrix();
            glTranslatef32(0, p->py * 512, p->pz * 512);
            glRotateXi(ang);
            glTranslatef32(0, -p->py * 512, -p->pz * 512);
            tex_box(p->x0, p->y0, p->z0, p->x1, p->y1, p->z1, p->tex, p->face_tex);
            glPopMatrix(1);
        } else {
            tex_box(p->x0, p->y0, p->z0, p->x1, p->y1, p->z1, p->tex, p->face_tex);
        }
    }
    glPopMatrix(1);
}

static void render_item(Entity *e)
{
    int32_t rx, ry, rz;
    render_rel(e->body.x, e->body.y, e->body.z, &rx, &ry, &rz);
    if (!on_screen(rx, ry, rz))
        return;
    cur_light = light_at(e);
    cur_hurt = false;
    int32_t bob = sinLerp(e->age * 300) >> 4;
    glPushMatrix();
    glTranslatef32(rx >> 2, (ry + 600 + bob) >> 2, rz >> 2);
    int id = e->item.id;
    bool cube = id < 256 && blocks[id].shape == SHAPE_CUBE;
    if (cube) {
        glRotateYi(e->age * 200);
        glScalef32(512, 512, 512);
        const BlockDef *b = &blocks[id];
        int n = e->item.count > 1 ? 2 : 1;
        for (int k = 0; k < n; k++) {
            int o = k * 2;
            // per-face textures: use top/side
            tex_box(-2 + o, 0 + o, -2 + o, 2 + o, 4 + o, 2 + o, b->tex[FACE_N], b->tex[FACE_N]);
        }
    } else {
        // billboard facing the camera
        glRotateYi(-cam.yaw);
        glScalef32(512, 512, 512);
        int icon = item_valid(id) ? items[id].icon : 0;
        GFX_PAL_FORMAT = 32;  // icon palette at 512 bytes / 16
        GFX_TEX_FORMAT = gx_texparam16(TEX_ICON_BASE + icon, TEXGEN_NONE);
        glBegin(GL_QUADS);
        set_material(FACE_U);
        glTexCoord2t16(0, 256); glVertex3v16(-4 * 512, 0, 0);
        glTexCoord2t16(256, 256); glVertex3v16(4 * 512, 0, 0);
        glTexCoord2t16(256, 0); glVertex3v16(4 * 512, 8 * 512, 0);
        glTexCoord2t16(0, 0); glVertex3v16(-4 * 512, 8 * 512, 0);
        glEnd();
        GFX_PAL_FORMAT = 0;
    }
    glPopMatrix(1);
}

static void render_arrow(Entity *e)
{
    int32_t rx, ry, rz;
    render_rel(e->body.x, e->body.y, e->body.z, &rx, &ry, &rz);
    if (!on_screen(rx, ry, rz))
        return;
    cur_light = light_at(e);
    cur_hurt = false;
    glPushMatrix();
    glTranslatef32(rx >> 2, ry >> 2, rz >> 2);
    glRotateYi(-e->yaw);
    glScalef32(512, 512, 512);
    GFX_TEX_FORMAT = gx_texparam16(TEX_LOG_BIRCH, TEXGEN_NONE);
    tex_box(0, 0, -6, 1, 1, 6, TEX_PLANKS_BIRCH, 0);
    tex_box(0, 0, 4, 1, 1, 6, TEX_WOOL_WHITE, 0);
    glPopMatrix(1);
}

static void render_tnt(Entity *e)
{
    int32_t rx, ry, rz;
    render_rel(e->body.x, e->body.y, e->body.z, &rx, &ry, &rz);
    if (!on_screen(rx, ry, rz))
        return;
    cur_light = light_at(e);
    cur_hurt = false;
    if ((e->fuse / 5) & 1)
        cur_light = 0xFF;
    glPushMatrix();
    glTranslatef32(rx >> 2, ry >> 2, rz >> 2);
    int s = 512 + ((e->fuse < 10) ? (10 - e->fuse) * 10 : 0);
    glScalef32(s, s, s);
    tex_box(-8, 0, -8, 8, 16, 8, TEX_TNT_SIDE, 0);
    glPopMatrix(1);
}

static void render_particles(void)
{
    cur_hurt = false;
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &particles[i];
        if (p->life <= 0)
            continue;
        int32_t rx, ry, rz;
        render_rel(p->x, p->y, p->z, &rx, &ry, &rz);
        if (!on_screen(rx, ry, rz))
            continue;
        cur_light = world_light_raw(p->x >> 12, p->y >> 12, p->z >> 12);
        glPushMatrix();
        glTranslatef32(rx >> 2, ry >> 2, rz >> 2);
        glRotateYi(-cam.yaw);
        glScalef32(512, 512, 512);
        GFX_TEX_FORMAT = gx_texparam16(p->tex, TEXGEN_NONE);
        int u = p->u * 16, v = p->v * 16;
        glBegin(GL_QUADS);
        set_material(FACE_U);
        glTexCoord2t16(u, v + 64); glVertex3v16(-512, -512, 0);
        glTexCoord2t16(u + 64, v + 64); glVertex3v16(512, -512, 0);
        glTexCoord2t16(u + 64, v); glVertex3v16(512, 512, 0);
        glTexCoord2t16(u, v); glVertex3v16(-512, 512, 0);
        glEnd();
        glPopMatrix(1);
    }
}

void entities_render(void)
{
    render_begin_entities();
    GFX_PAL_FORMAT = 0;
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_BACK | POLY_FORMAT_LIGHT0 | POLY_FOG | POLY_ID(0));
    for (int i = 0; i < MAX_ENTITIES; i++) {
        Entity *e = &entities[i];
        switch (e->type) {
            case ET_NONE: break;
            case ET_ITEM:
                glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_FORMAT_LIGHT0 | POLY_FOG | POLY_ID(0));
                render_item(e);
                glPolyFmt(POLY_ALPHA(31) | POLY_CULL_BACK | POLY_FORMAT_LIGHT0 | POLY_FOG | POLY_ID(0));
                break;
            case ET_ARROW: render_arrow(e); break;
            case ET_TNT: render_tnt(e); break;
            default: render_mob(e); break;
        }
    }
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_FORMAT_LIGHT0 | POLY_FOG | POLY_ID(0));
    render_particles();
    render_end_entities();
}
