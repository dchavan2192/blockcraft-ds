// World storage and lighting
#include <stdlib.h>
#include "world.h"

World world;

#define LQ_SIZE 32768
#define LQ_MASK (LQ_SIZE - 1)
static uint32_t *lq_add;     // ring buffer for light increase (index)
static uint32_t *lq_rem;     // ring buffer for light removal (index | level << 24)
static int add_head, add_tail;
static int rem_head, rem_tail;

bool world_alloc(int size)
{
    world_free();
    world.size = size;
    world.mask = size - 1;
    world.shift = (size == 256) ? 8 : 7;
    world.chunks = size / SEC_SIZE;
    size_t n = (size_t)size * size * WORLD_H;
    world.blk = malloc(n);
    world.light = malloc(n);
    world.height = malloc(size * size);
    if (!lq_add)
        lq_add = malloc(LQ_SIZE * sizeof(uint32_t));
    if (!lq_rem)
        lq_rem = malloc(LQ_SIZE * sizeof(uint32_t));
    if (!world.blk || !world.light || !world.height || !lq_add || !lq_rem) {
        world_free();
        return false;
    }
    memset(world.blk, 0, n);
    memset(world.light, 0, n);
    memset(world.height, 0, size * size);
    return true;
}

void world_free(void)
{
    free(world.blk);
    free(world.light);
    free(world.height);
    world.blk = NULL;
    world.light = NULL;
    world.height = NULL;
}

// ---------------------------------------------------------------------------
// Heightmap
// ---------------------------------------------------------------------------

static void column_height(int x, int z)
{
    uint8_t *col = &world.blk[widx(x, 0, z)];
    int y = WORLD_H - 1;
    while (y >= 0 && blocks[col[y]].opacity == 0)
        y--;
    world.height[((z & world.mask) << world.shift) + (x & world.mask)] = y + 1;
}

void world_compute_heightmap(void)
{
    for (int z = 0; z < world.size; z++)
        for (int x = 0; x < world.size; x++)
            column_height(x, z);
}

// ---------------------------------------------------------------------------
// Light propagation
// ---------------------------------------------------------------------------

static inline void idx_decode(uint32_t idx, int *x, int *y, int *z)
{
    *y = idx & 63;
    uint32_t col = idx >> 6;
    *x = col & world.mask;
    *z = col >> world.shift;
}

static uint8_t opac_tab[256];
static uint8_t emit_tab[256];

static void build_tables(void)
{
    for (int i = 0; i < 256; i++) {
        opac_tab[i] = blocks[i].opacity;
        emit_tab[i] = blocks[i].emit;
    }
}

static inline int opacity_at(uint32_t idx)
{
    return opac_tab[world.blk[idx]];
}

// Neighbor index in direction f, or -1 if outside vertically
static inline int32_t neighbor_idx(int x, int y, int z, int f)
{
    uint32_t idx = (((z << world.shift) + x) << 6) + y;
    switch (f) {
        case FACE_D: return y > 0 ? (int32_t)idx - 1 : -1;
        case FACE_U: return y < WORLD_H - 1 ? (int32_t)idx + 1 : -1;
        case FACE_W: return x > 0 ? (int32_t)idx - 64 : (int32_t)idx + (world.mask << 6);
        case FACE_E: return x < world.mask ? (int32_t)idx + 64 : (int32_t)idx - (world.mask << 6);
        case FACE_N: return z > 0 ? (int32_t)idx - (64 << world.shift) : (int32_t)idx + ((world.mask << world.shift) << 6);
        default: return z < world.mask ? (int32_t)idx + (64 << world.shift) : (int32_t)idx - ((world.mask << world.shift) << 6);
    }
}

static inline void push_add(uint32_t idx)
{
    lq_add[add_tail] = idx;
    add_tail = (add_tail + 1) & LQ_MASK;
}

static inline void push_rem(uint32_t idx, int level)
{
    lq_rem[rem_tail] = idx | ((uint32_t)level << 24);
    rem_tail = (rem_tail + 1) & LQ_MASK;
}

static inline int queue_len(int head, int tail)
{
    return (tail - head) & LQ_MASK;
}

// Generic light accessors: channel 0 = sky (high nibble), 1 = block (low nibble)
static inline int lget(uint32_t idx, int ch)
{
    return ch == 0 ? world.light[idx] >> 4 : world.light[idx] & 15;
}

static inline void lset(uint32_t idx, int ch, int v)
{
    if (ch == 0)
        world.light[idx] = (world.light[idx] & 0x0F) | (v << 4);
    else
        world.light[idx] = (world.light[idx] & 0xF0) | v;
}

static bool mark_dirty_enabled;

static inline void mark_idx(uint32_t idx)
{
    if (mark_dirty_enabled) {
        int x, y, z;
        idx_decode(idx, &x, &y, &z);
        mesh_mark_block(x, y, z);
    }
}

ITCM_CODE ARM_CODE static void propagate_add(int ch)
{
    while (add_head != add_tail) {
        uint32_t idx = lq_add[add_head];
        add_head = (add_head + 1) & LQ_MASK;
        int L = lget(idx, ch);
        if (L <= 1)
            continue;
        int x, y, z;
        idx_decode(idx, &x, &y, &z);
        for (int f = 0; f < 6; f++) {
            int32_t n = neighbor_idx(x, y, z, f);
            if (n < 0)
                continue;
            int op = opacity_at(n);
            if (op >= 15)
                continue;
            int nl;
            if (ch == 0 && f == FACE_D && L == 15 && op == 0)
                nl = 15;
            else
                nl = L - (op > 1 ? op : 1);
            if (nl > lget(n, ch)) {
                lset(n, ch, nl);
                mark_idx(n);
                if (queue_len(add_head, add_tail) < LQ_SIZE - 2)
                    push_add(n);
            }
        }
    }
}

ITCM_CODE ARM_CODE static void propagate_remove(int ch)
{
    while (rem_head != rem_tail) {
        uint32_t e = lq_rem[rem_head];
        rem_head = (rem_head + 1) & LQ_MASK;
        uint32_t idx = e & 0xFFFFFF;
        int L = e >> 24;
        int x, y, z;
        idx_decode(idx, &x, &y, &z);
        for (int f = 0; f < 6; f++) {
            int32_t n = neighbor_idx(x, y, z, f);
            if (n < 0)
                continue;
            int nl = lget(n, ch);
            if (nl == 0)
                continue;
            bool from_here = nl < L || (ch == 0 && f == FACE_D && L == 15 && nl == 15);
            if (from_here) {
                // Keep emitters
                int emit = ch == 1 ? emit_tab[world.blk[n]] : 0;
                lset(n, ch, emit);
                mark_idx(n);
                if (emit)
                    push_add(n);
                if (queue_len(rem_head, rem_tail) < LQ_SIZE - 2)
                    push_rem(n, nl);
            } else {
                push_add(n);
            }
        }
    }
}

ARM_CODE void world_init_light(void (*progress)(int pct))
{
    build_tables();
    mark_dirty_enabled = false;
    size_t total = (size_t)world.size * world.size * WORLD_H;
    memset(world.light, 0, total);
    world_compute_heightmap();
    int ncol = world.size * world.size;
    uint8_t *bottom = malloc(ncol); // lowest y with full sky light
    // Column sky light
    for (int c = 0; c < ncol; c++) {
        uint32_t base = (uint32_t)c << 6;
        const uint8_t *col = &world.blk[base];
        uint8_t *lcol = &world.light[base];
        int L = 15;
        int b = WORLD_H;
        for (int y = WORLD_H - 1; y >= 0 && L > 0; y--) {
            int op = opac_tab[col[y]];
            if (!(L == 15 && op == 0))
                L = L - (op > 1 ? op : 1);
            if (L < 0)
                L = 0;
            lcol[y] = L << 4;
            if (L == 15)
                b = y;
        }
        if (bottom)
            bottom[c] = b;
    }
    if (progress)
        progress(20);
    // Seed cells that can spread light sideways into darker neighbors
    add_head = add_tail = 0;
    for (int z = 0; z < world.size; z++) {
        for (int x = 0; x < world.size; x++) {
            int c = (z << world.shift) + x;
            uint32_t base = (uint32_t)c << 6;
            int b = bottom ? bottom[c] : 0;
            int maxb = b;
            if (bottom) {
                int n1 = bottom[(z << world.shift) + ((x + 1) & world.mask)];
                int n2 = bottom[(z << world.shift) + ((x - 1) & world.mask)];
                int n3 = bottom[(((z + 1) & world.mask) << world.shift) + x];
                int n4 = bottom[(((z - 1) & world.mask) << world.shift) + x];
                maxb = imax(imax(n1, n2), imax(n3, n4));
            } else {
                maxb = WORLD_H;
            }
            for (int y = b; y < maxb && y < WORLD_H; y++) {
                push_add(base + y);
            }
            // partially lit cells under leaves / water
            for (int y = b - 1; y >= 0; y--) {
                int L = world.light[base + y] >> 4;
                if (L == 0)
                    break;
                if (L > 1)
                    push_add(base + y);
            }
            if (queue_len(add_head, add_tail) > LQ_SIZE / 2)
                propagate_add(0);
        }
        if (progress && (z & 15) == 0)
            progress(20 + z * 60 / world.size);
    }
    propagate_add(0);
    free(bottom);
    // Block light from emitters
    add_head = add_tail = 0;
    for (size_t i = 0; i < total; i++) {
        int e = emit_tab[world.blk[i]];
        if (e) {
            world.light[i] = (world.light[i] & 0xF0) | e;
            push_add(i);
            if (queue_len(add_head, add_tail) > LQ_SIZE / 2)
                propagate_add(1);
        }
    }
    propagate_add(1);
    if (progress)
        progress(100);
    mark_dirty_enabled = true;
}

void light_update_block(int x, int y, int z, int old_id, int new_id)
{
    if (!opac_tab[B_STONE])
        build_tables();
    mark_dirty_enabled = true;
    uint32_t idx = widx(x, y, z);
    const BlockDef *ob = &blocks[old_id];
    const BlockDef *nb = &blocks[new_id];

    // ---- Block light ----
    if (ob->emit != nb->emit || ob->opacity != nb->opacity) {
        int cur = world.light[idx] & 15;
        rem_head = rem_tail = 0;
        add_head = add_tail = 0;
        if (cur > 0) {
            lset(idx, 1, 0);
            push_rem(idx, cur);
            propagate_remove(1);
        }
        if (nb->emit) {
            lset(idx, 1, nb->emit);
            push_add(idx);
        }
        // Re-seed from neighbors if now more transparent
        if (nb->opacity < 15) {
            for (int f = 0; f < 6; f++) {
                int32_t n = neighbor_idx(x, y, z, f);
                if (n >= 0 && (world.light[n] & 15) > 1)
                    push_add(n);
            }
        }
        propagate_add(1);
    }

    // ---- Sky light ----
    if (ob->opacity != nb->opacity) {
        rem_head = rem_tail = 0;
        add_head = add_tail = 0;
        int cur = world.light[idx] >> 4;
        if (cur > 0) {
            lset(idx, 0, 0);
            push_rem(idx, cur);
            propagate_remove(0);
        }
        if (nb->opacity < 15) {
            for (int f = 0; f < 6; f++) {
                int32_t n = neighbor_idx(x, y, z, f);
                if (n >= 0 && (world.light[n] >> 4) > 0)
                    push_add(n);
            }
            if (y == WORLD_H - 1) {
                lset(idx, 0, 15);
                push_add(idx);
            }
        }
        propagate_add(0);
        column_height(x, z);
    }
    mesh_mark_block(x, y, z);
}

void world_set(int x, int y, int z, int id)
{
    if (y < 0 || y >= WORLD_H)
        return;
    uint32_t idx = widx(x, y, z);
    int old = world.blk[idx];
    if (old == id)
        return;
    world.blk[idx] = id;
    light_update_block(x, y, z, old, id);
}
