// AABB collision
#include "physics.h"
#include "world.h"
#include "blocks.h"

int block_boxes(int bx, int by, int bz, AABB *out)
{
    int id = world_get(bx, by, bz);
    const BlockDef *b = &blocks[id];
    if (!(b->flags & BF_SOLID))
        return 0;
    int32_t x0 = bx << 12, y0 = by << 12, z0 = bz << 12;
    int32_t x1 = x0 + 4096, y1 = y0 + 4096, z1 = z0 + 4096;
    switch (b->shape) {
        case SHAPE_FARMLAND:
            y1 = y0 + 15 * 256;
            break;
        case SHAPE_BED:
            y1 = y0 + 9 * 256;
            break;
        case SHAPE_CACTUS:
            x0 += 256; x1 -= 256; z0 += 256; z1 -= 256;
            break;
        case SHAPE_DOOR: {
            int idx = id - B_DOOR;
            int f = idx & 3, open = (idx >> 2) & 1;
            int side = open ? ((f + 1) & 3) : ((f + 2) & 3);   // closed: edge nearest the placer
            switch (side) {
                case 0: z1 = z0 + 3 * 256; break;
                case 1: x0 = x1 - 3 * 256; break;
                case 2: z0 = z1 - 3 * 256; break;
                default: x1 = x0 + 3 * 256; break;
            }
            break;
        }
        default:
            break;
    }
    out->x0 = x0; out->y0 = y0; out->z0 = z0;
    out->x1 = x1; out->y1 = y1; out->z1 = z1;
    return 1;
}

#define MAXBOX 96
static AABB boxes[MAXBOX];
static int nboxes;

static void gather_boxes(int32_t x0, int32_t y0, int32_t z0, int32_t x1, int32_t y1, int32_t z1)
{
    nboxes = 0;
    int bx0 = x0 >> 12, by0 = y0 >> 12, bz0 = z0 >> 12;
    int bx1 = (x1 - 1) >> 12, by1 = (y1 - 1) >> 12, bz1 = (z1 - 1) >> 12;
    by0 -= 1; // tall blocks below (none currently, but keep doors etc.)
    for (int by = by0; by <= by1; by++)
        for (int bz = bz0; bz <= bz1; bz++)
            for (int bx = bx0; bx <= bx1; bx++) {
                if (nboxes >= MAXBOX)
                    return;
                nboxes += block_boxes(bx, by, bz, &boxes[nboxes]);
            }
}

static int32_t clip_y(const AABB *e, int32_t dy)
{
    for (int i = 0; i < nboxes; i++) {
        const AABB *b = &boxes[i];
        if (e->x1 <= b->x0 || e->x0 >= b->x1 || e->z1 <= b->z0 || e->z0 >= b->z1)
            continue;
        if (dy > 0 && e->y1 <= b->y0) {
            int32_t m = b->y0 - e->y1;
            if (m < dy) dy = m;
        } else if (dy < 0 && e->y0 >= b->y1) {
            int32_t m = b->y1 - e->y0;
            if (m > dy) dy = m;
        }
    }
    return dy;
}

static int32_t clip_x(const AABB *e, int32_t dx)
{
    for (int i = 0; i < nboxes; i++) {
        const AABB *b = &boxes[i];
        if (e->y1 <= b->y0 || e->y0 >= b->y1 || e->z1 <= b->z0 || e->z0 >= b->z1)
            continue;
        if (dx > 0 && e->x1 <= b->x0) {
            int32_t m = b->x0 - e->x1;
            if (m < dx) dx = m;
        } else if (dx < 0 && e->x0 >= b->x1) {
            int32_t m = b->x1 - e->x0;
            if (m > dx) dx = m;
        }
    }
    return dx;
}

static int32_t clip_z(const AABB *e, int32_t dz)
{
    for (int i = 0; i < nboxes; i++) {
        const AABB *b = &boxes[i];
        if (e->y1 <= b->y0 || e->y0 >= b->y1 || e->x1 <= b->x0 || e->x0 >= b->x1)
            continue;
        if (dz > 0 && e->z1 <= b->z0) {
            int32_t m = b->z0 - e->z1;
            if (m < dz) dz = m;
        } else if (dz < 0 && e->z0 >= b->z1) {
            int32_t m = b->z1 - e->z0;
            if (m > dz) dz = m;
        }
    }
    return dz;
}

static inline void make_aabb(const Body *b, int32_t x, int32_t y, int32_t z, AABB *e)
{
    e->x0 = x - b->hw; e->x1 = x + b->hw;
    e->z0 = z - b->hw; e->z1 = z + b->hw;
    e->y0 = y; e->y1 = y + b->h;
}

ARM_CODE void body_move(Body *b, int32_t dx, int32_t dy, int32_t dz, int32_t step)
{
    AABB e;
    make_aabb(b, b->x, b->y, b->z, &e);
    int32_t gx0 = imin(e.x0, e.x0 + dx), gx1 = imax(e.x1, e.x1 + dx);
    int32_t gy0 = imin(e.y0, e.y0 + dy), gy1 = imax(e.y1, e.y1 + dy) + step;
    int32_t gz0 = imin(e.z0, e.z0 + dz), gz1 = imax(e.z1, e.z1 + dz);
    gather_boxes(gx0, gy0, gz0, gx1, gy1, gz1);

    int32_t odx = dx, ody = dy, odz = dz;
    bool was_ground = b->on_ground;

    int32_t cy = clip_y(&e, dy);
    e.y0 += cy; e.y1 += cy;
    int32_t cx = clip_x(&e, dx);
    e.x0 += cx; e.x1 += cx;
    int32_t cz = clip_z(&e, dz);
    e.z0 += cz; e.z1 += cz;

    // Step up (e.g. onto farmland, beds) when blocked horizontally on the ground
    if (step > 0 && (was_ground || (ody < 0 && cy != ody)) && (cx != odx || cz != odz)) {
        AABB s;
        make_aabb(b, b->x, b->y, b->z, &s);
        int32_t up = clip_y(&s, step);
        s.y0 += up; s.y1 += up;
        int32_t sx = clip_x(&s, odx);
        s.x0 += sx; s.x1 += sx;
        int32_t sz = clip_z(&s, odz);
        s.z0 += sz; s.z1 += sz;
        int32_t down = clip_y(&s, -up + (ody < 0 ? ody : 0));
        s.y0 += down; s.y1 += down;
        int64_t d_orig = (int64_t)cx * cx + (int64_t)cz * cz;
        int64_t d_step = (int64_t)sx * sx + (int64_t)sz * sz;
        if (d_step > d_orig) {
            e = s;
            cx = sx;
            cz = sz;
            cy = s.y0 - b->y;
        }
    }

    b->hit_x = cx != odx;
    b->hit_z = cz != odz;
    b->hit_y = cy != ody;
    b->on_ground = (ody < 0 && cy != ody);
    b->x = world_wrap_fx(e.x0 + b->hw);
    b->z = world_wrap_fx(e.z0 + b->hw);
    b->y = e.y0;
}

bool body_collides(const Body *b)
{
    AABB e;
    make_aabb(b, b->x, b->y, b->z, &e);
    gather_boxes(e.x0, e.y0, e.z0, e.x1, e.y1, e.z1);
    for (int i = 0; i < nboxes; i++) {
        const AABB *o = &boxes[i];
        if (e.x1 > o->x0 && e.x0 < o->x1 && e.y1 > o->y0 && e.y0 < o->y1 && e.z1 > o->z0 && e.z0 < o->z1)
            return true;
    }
    return false;
}

static bool body_any(const Body *b, bool (*pred)(int id), int32_t shrink)
{
    int32_t x0 = b->x - b->hw + shrink, x1 = b->x + b->hw - shrink;
    int32_t z0 = b->z - b->hw + shrink, z1 = b->z + b->hw - shrink;
    int32_t y0 = b->y + shrink, y1 = b->y + b->h - shrink;
    for (int y = y0 >> 12; y <= (y1 >> 12); y++)
        for (int z = z0 >> 12; z <= (z1 >> 12); z++)
            for (int x = x0 >> 12; x <= (x1 >> 12); x++) {
                int id = world_get(x, y, z);
                if (pred(id)) {
                    // liquid surface height check
                    if (block_liquid(id)) {
                        int32_t top = (y << 12) + liquid_height16(id) * 256;
                        if (y0 > top)
                            continue;
                    }
                    return true;
                }
            }
    return false;
}

static bool pred_water(int id) { return is_water(id); }
static bool pred_lava(int id) { return is_lava(id); }
static uint16_t touch_flag;
static bool pred_flag(int id) { return (blocks[id].flags & touch_flag) != 0; }

bool body_in_water(const Body *b) { return body_any(b, pred_water, 400); }
bool body_in_lava(const Body *b) { return body_any(b, pred_lava, 400); }
bool body_touching(const Body *b, uint16_t flag)
{
    touch_flag = flag;
    return body_any(b, pred_flag, -64);
}
