// The player
#pragma once
#include "common.h"
#include "items.h"
#include "physics.h"
#include "inventory.h"

typedef struct {
    Body body;
    int32_t vx, vy, vz;
    int32_t prev_x, prev_y, prev_z;
    int yaw, pitch;
    bool in_water, in_lava, head_in_water, on_ladder;
    bool flying, sneaking, sprinting;
    int32_t fall_dist;
    int health;          // 0..20
    int food;            // 0..20
    int32_t saturation;  // fx (0..20)
    int32_t exhaustion;  // fx
    int air;             // ticks (300 = full)
    int hurt_time;       // flash timer
    int invuln;          // invulnerability ticks
    int regen_timer;
    int fire;            // burning ticks
    int mode;
    bool dead;
    ItemStack inv[INV_SIZE];
    ItemStack armor[4];
    ItemStack cursor;    // item held by the stylus in menus
    ItemStack craft[4];  // 2x2 crafting grid
    int sel;
    int xp_level;          // experience level
    int xp_points;         // progress toward the next level
    // mining
    bool mining;
    int mine_x, mine_y, mine_z;
    int32_t mine_progress;   // 0..4096
    int mine_cooldown;
    int swing;               // hand swing animation ticks
    int eat_timer;           // >0 while eating
    int bow_timer;
    int attack_cd;
    int spawn_x, spawn_y, spawn_z;
    bool has_bed_spawn;
    int sleep_timer;
    int jump_tap_timer;      // for double-tap fly toggle
    int sprint_tap_timer;
    int step_dist;           // for footstep sounds
    // targeting
    bool target_valid;
    int tx, ty, tz, tface;
    int target_entity;       // -1 none
} Player;

extern Player player;

void player_reset(int mode);
void player_spawn(void);
void player_tick(void);            // 20 Hz
void player_frame(int vblanks);    // per rendered frame: look and targeting
void player_camera(int32_t alpha); // update render camera (alpha 0..4096 interpolation)
ItemStack *player_held(void);
void player_damage(int amount, int cause);
void player_heal(int amount);
void player_add_xp(int points);
int xp_to_next_level(int level);
void player_add_exhaustion(int32_t fx);
int player_armor_points(void);
bool player_give(int id, int count, int damage);
void player_drop_held(bool whole_stack);
void player_drop_stack(ItemStack *s);
void player_respawn(void);
int player_facing(void);   // horizontal facing 0..3 (N,E,S,W)
bool player_eye_in(uint16_t flag);

enum { DMG_GENERIC = 0, DMG_FALL, DMG_DROWN, DMG_LAVA, DMG_FIRE, DMG_MOB, DMG_EXPLOSION, DMG_STARVE, DMG_CACTUS, DMG_ARROW, DMG_VOID };
extern const char *death_message;
