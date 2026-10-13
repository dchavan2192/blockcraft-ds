// AABB collision against the block world
#pragma once
#include "common.h"

typedef struct {
    int32_t x0, y0, z0, x1, y1, z1;
} AABB;

// Collision boxes for a block at integer position (fx world coords). Returns count.
int block_boxes(int bx, int by, int bz, AABB *out);

typedef struct {
    int32_t x, y, z;        // feet center (fx)
    int32_t hw, h;          // half width, height (fx)
    bool on_ground;
    bool hit_x, hit_z, hit_y;
} Body;

// Move body by (dx,dy,dz) with collision; step = max step-up height (fx)
void body_move(Body *b, int32_t dx, int32_t dy, int32_t dz, int32_t step);
bool body_collides(const Body *b);
// Liquid checks over the body volume
bool body_in_water(const Body *b);
bool body_in_lava(const Body *b);
bool body_touching(const Body *b, uint16_t flag);
