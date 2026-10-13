// World storage, block access and lighting
#pragma once
#include "common.h"
#include "blocks.h"

typedef struct {
    int size;          // width and depth in blocks (128 or 256, power of two)
    int mask;          // size - 1
    int shift;         // log2(size)
    int chunks;        // size / 16
    uint8_t *blk;      // block ids, index = ((z << shift) + x) * WORLD_H + y
    uint8_t *light;    // (sky << 4) | block
    uint8_t *height;   // first y above the highest light-blocking block, per column
    uint32_t seed;
    int mode;
    int32_t time;      // time of day 0..23999 (0 = sunrise)
    uint32_t ticks;    // total ticks elapsed
    int spawn_x, spawn_y, spawn_z;
    char name[24];
} World;

extern World world;

bool world_alloc(int size);
void world_free(void);

static inline int widx(int x, int y, int z)
{
    x &= world.mask;
    z &= world.mask;
    return (((z << world.shift) + x) << 6) + y;
}

static inline int world_get(int x, int y, int z)
{
    if (y < 0)
        return B_BEDROCK;
    if (y >= WORLD_H)
        return B_AIR;
    return world.blk[widx(x, y, z)];
}

static inline int world_light_raw(int x, int y, int z)
{
    if (y >= WORLD_H)
        return 0xF0;
    if (y < 0)
        return 0;
    return world.light[widx(x, y, z)];
}

static inline int world_skylight(int x, int y, int z) { return world_light_raw(x, y, z) >> 4; }
static inline int world_blocklight(int x, int y, int z) { return world_light_raw(x, y, z) & 15; }

static inline int world_height(int x, int z)
{
    return world.height[((z & world.mask) << world.shift) + (x & world.mask)];
}

// Raw set: no lighting/mesh updates (world generation)
static inline void world_set_raw(int x, int y, int z, int id)
{
    if (y < 0 || y >= WORLD_H)
        return;
    world.blk[widx(x, y, z)] = id;
}

// Full set: updates heightmap, lighting and marks meshes dirty
void world_set(int x, int y, int z, int id);

// Lighting
void world_compute_heightmap(void);
void world_init_light(void (*progress)(int pct));
void light_update_block(int x, int y, int z, int old_id, int new_id);

// Wrap a coordinate difference into [-size/2, size/2)
static inline int world_wrap_delta(int d)
{
    int h = world.size >> 1;
    d &= world.mask;
    if (d >= h)
        d -= world.size;
    return d;
}

static inline int32_t world_wrap_delta_fx(int32_t d)
{
    int32_t sz = world.size << FX_SHIFT;
    int32_t h = sz >> 1;
    d = d & (sz - 1);
    if (d >= h)
        d -= sz;
    return d;
}

static inline int32_t world_wrap_fx(int32_t v)
{
    return v & ((world.size << FX_SHIFT) - 1);
}

// Implemented by mesh.c
void mesh_mark_block(int x, int y, int z);
