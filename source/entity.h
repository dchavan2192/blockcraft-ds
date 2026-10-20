// Entities: dropped items, mobs, projectiles, primed TNT
#pragma once
#include "common.h"
#include "items.h"
#include "physics.h"

enum {
    ET_NONE = 0, ET_ITEM, ET_PIG, ET_COW, ET_SHEEP, ET_CHICKEN,
    ET_ZOMBIE, ET_SKELETON, ET_SPIDER, ET_SHROOM, ET_ARROW, ET_TNT, ET_XP,
    ET_COUNT
};

#define MAX_ENTITIES 64

typedef struct {
    uint8_t type;
    uint8_t on_ground;
    uint8_t in_water;
    uint8_t flags;        // EF_*
    Body body;
    int32_t vx, vy, vz;
    int16_t yaw;          // body yaw (angle units)
    int16_t head_pitch;
    int16_t health;
    int16_t age;
    int16_t timer;        // AI timer
    int16_t hurt;         // red flash
    int16_t attack_cd;
    int16_t fuse;
    int16_t pickup_delay;
    int16_t burn;
    int32_t fall;
    int32_t walk_phase;
    int32_t walk_amt;
    int16_t target_yaw;
    int8_t ai;            // AI state
    int8_t sheared;
    ItemStack item;
} Entity;

#define EF_FROM_PLAYER 1
#define EF_STUCK 2

extern Entity entities[MAX_ENTITIES];

void entities_clear(void);
int entity_spawn(int type, int32_t x, int32_t y, int32_t z);
void entity_spawn_item(int32_t x, int32_t y, int32_t z, const ItemStack *s);
void entity_spawn_item_vel(int32_t x, int32_t y, int32_t z, const ItemStack *s, int32_t vx, int32_t vy, int32_t vz);
void entities_tick(void);
// Experience orbs worth `amount` points in total (split into a few orbs)
void entity_spawn_xp(int32_t x, int32_t y, int32_t z, int amount);
int entity_raycast(int32_t ex, int32_t ey, int32_t ez, int32_t dx, int32_t dy, int32_t dz, int32_t maxd, int32_t *out_t);
void entity_damage(int idx, int dmg, int32_t kx, int32_t kz, bool by_player);
bool entity_interact(int idx, ItemStack *held);
void explosion(int32_t x, int32_t y, int32_t z, int radius);
void entities_spawn_initial(void);
void entity_shoot_arrow(int32_t x, int32_t y, int32_t z, int32_t dx, int32_t dy, int32_t dz, int32_t speed, bool from_player);
int entities_count(int type);
const char *entity_name(int type);

// Particles
void particles_block(int x, int y, int z, int block_id);
void particles_clear(void);
