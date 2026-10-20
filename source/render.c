// World renderer: section visibility traversal, display list submission, sky and fog
#include <stdlib.h>
#include "render.h"
#include "world.h"
#include "mesh.h"
#include "gx.h"
#include "gfx.h"
#include "assets_gen.h"

Camera cam;
int render_distance = 3;
uint32_t render_frame_no;
int stat_sections_drawn, stat_polys, stat_verts, stat_builds, stat_budget, stat_bfs, stat_budget_hit;
uint8_t sky_daylight = 31;
uint16_t sky_color, fog_color;

static int32_t sin_yaw, cos_yaw, sin_pitch, cos_pitch;

#define NEAR_PLANE 0.2
#define MAX_VIS 1024

typedef struct {
    int16_t dcx, dcz;   // chunk offset from camera chunk
    int8_t sy;
    int8_t enter;       // face we entered through (-1 for start)
    uint8_t dirs;       // directions traveled
    uint8_t pad;
} BfsNode;

static BfsNode bfs_queue[MAX_VIS];

typedef struct {
    Section *sec;
    int32_t rx, ry, rz;      // origin relative to camera (fx, blocks)
    uint8_t dirmask;         // directions worth drawing
    uint8_t draw_dark;       // also draw unlit faces
    int32_t dist;            // squared distance (1/16 block units)
} VisSec;

static VisSec vis[MAX_VIS];
static int nvis;

typedef struct {
    int idx;
    int dist;
    int32_t d2;     // squared distance, same units as VisSec.dist
} Pending;
static int32_t budget_cut_d2;   // squared distance of the first section dropped by the budget
static Pending pending[MAX_VIS];
static int npending;

static bool target_valid;
static int target_x, target_y, target_z, target_crack = -1;

static int32_t frustum_tx, frustum_ty, frustum_kx, frustum_ky;
static int fog_shift_v, fog_offset_v;

void gx_call(const uint32_t *words, int count)
{
    if (count <= 0)
        return;
    while (dmaBusy(0) || dmaBusy(1) || dmaBusy(2) || dmaBusy(3))
        ;
    dmaSetParams(0, words, (void *)&GFX_FIFO, DMA_FIFO | count);
    while (dmaBusy(0))
        ;
}

static int32_t depth_at(int32_t dist_fx)
{
    // z-buffer depth for perspective with near/far planes (approximation of hardware mapping)
    int64_t n = (int64_t)(NEAR_PLANE * 4096);
    int64_t f = (int64_t)(render_distance * 16 + 24) << 12;
    // z/w = (f+n)/(f-n) - 2fn/((f-n) d)
    int64_t a = ((f + n) << 16) / (f - n);
    int64_t b = (((2 * f * n) >> 12) << 16) / (f - n);
    int64_t zw = a - ((b << 12) / dist_fx);    // 16.16
    int64_t depth = ((zw + 65536) * 0x7FFF) >> 17;
    if (depth < 0)
        depth = 0;
    if (depth > 0x7FFF)
        depth = 0x7FFF;
    return (int32_t)depth;
}

static void setup_fog_end(int32_t d1)
{
    int32_t d0 = d1 - (14 << 12);
    if (d0 < FX(2))
        d0 = FX(2);
    int32_t z0 = depth_at(d0), z1 = depth_at(d1);
    int span = z1 - z0;
    if (span < 32)
        span = 32;
    // each of 32 entries covers 0x400 >> shift
    int shift = 10;
    while (shift > 0 && (32 * (0x400 >> shift)) < span)
        shift--;
    fog_shift_v = shift;
    fog_offset_v = z0;
}

static void setup_fog(void)
{
    setup_fog_end((render_distance * 16 - 2) << 12);
}

void render_init(void)
{
    // tan(35 deg) = 0.700, aspect 4:3 -> 0.934
    frustum_ty = 2868;
    frustum_tx = 3825;
    frustum_ky = 5000;  // sqrt(1+t^2) in fx
    frustum_kx = 5600;
    setup_fog();
}

void render_set_target(bool valid, int x, int y, int z, int crack)
{
    target_valid = valid;
    target_x = x;
    target_y = y;
    target_z = z;
    target_crack = crack;
}

void camera_forward(int32_t *dx, int32_t *dy, int32_t *dz)
{
    int32_t sy = sinLerp(cam.yaw), cy = cosLerp(cam.yaw);
    int32_t sp = sinLerp(cam.pitch), cp = cosLerp(cam.pitch);
    *dx = fxmul(sy, cp);
    *dy = -sp;
    *dz = -fxmul(cy, cp);
}

void render_rel(int32_t wx, int32_t wy, int32_t wz, int32_t *rx, int32_t *ry, int32_t *rz)
{
    *rx = world_wrap_delta_fx(wx - cam.x);
    *ry = wy - cam.y;
    *rz = world_wrap_delta_fx(wz - cam.z);
}

// Sphere visibility in view space
ITCM_CODE ARM_CODE static bool sphere_visible(int32_t cx, int32_t cy, int32_t cz, int32_t r)
{
    int32_t vx = fxmul(cx, cos_yaw) + fxmul(cz, sin_yaw);
    int32_t vz1 = -fxmul(cx, sin_yaw) + fxmul(cz, cos_yaw);
    int32_t vy = fxmul(cy, cos_pitch) - fxmul(vz1, sin_pitch);
    int32_t vz = fxmul(cy, sin_pitch) + fxmul(vz1, cos_pitch);
    int32_t depth = -vz;
    if (depth < -r)
        return false;
    if (iabs(vx) > fxmul(depth, frustum_tx) + fxmul(r, frustum_kx))
        return false;
    if (iabs(vy) > fxmul(depth, frustum_ty) + fxmul(r, frustum_ky))
        return false;
    return true;
}

#define POLY_BUDGET 1450
static int poly_budget_used;
static bool budget_hit;
static int32_t dropped_dist_fx;
bool cam_underwater_fog;

// Faces of the camera's section reachable from the camera cell through open cells
static uint8_t cam_visited[512];  // bitset of 4096 cells
static uint16_t cam_stack[4096];

static uint8_t camera_exit_mask(int camx, int camy, int camz)
{
    int bx = (camx & world.mask) & ~15, by = camy & ~15, bz = (camz & world.mask) & ~15;
    int sx = camx & 15, sy = camy & 15, sz = camz & 15;
    if (camy < 0 || camy >= WORLD_H)
        return 0x3F;
    if (block_opaque(world_get(camx, camy, camz)))
        return 0x3F;
    memset(cam_visited, 0, sizeof(cam_visited));
    int sp = 0;
    int start = (sy << 8) | (sz << 4) | sx;
    cam_stack[sp++] = start;
    cam_visited[start >> 3] |= 1 << (start & 7);
    uint8_t faces = 0;
    while (sp) {
        int c = cam_stack[--sp];
        int x = c & 15, z = (c >> 4) & 15, y = c >> 8;
        if (x == 0) faces |= 1 << FACE_W;
        if (x == 15) faces |= 1 << FACE_E;
        if (y == 0) faces |= 1 << FACE_D;
        if (y == 15) faces |= 1 << FACE_U;
        if (z == 0) faces |= 1 << FACE_N;
        if (z == 15) faces |= 1 << FACE_S;
        if (faces == 0x3F)
            break;
        for (int f = 0; f < 6; f++) {
            int nx = x + face_dx[f], ny = y + face_dy[f], nz = z + face_dz[f];
            if ((unsigned)nx > 15 || (unsigned)ny > 15 || (unsigned)nz > 15)
                continue;
            int n = (ny << 8) | (nz << 4) | nx;
            if (cam_visited[n >> 3] & (1 << (n & 7)))
                continue;
            cam_visited[n >> 3] |= 1 << (n & 7);
            if (!block_opaque(world_get(bx + nx, by + ny, bz + nz)))
                cam_stack[sp++] = n;
        }
    }
    return faces;
}

static bool cam_outdoors;
static uint8_t cached_exit_mask = 0x3F;
static int cached_cx = -9999, cached_cy, cached_cz;
static uint32_t cached_dirty_stamp;

ITCM_CODE ARM_CODE static void traverse(void)
{
    nvis = 0;
    poly_budget_used = 0;
    budget_hit = false;
    npending = 0;
    render_frame_no++;
    int camx = cam.x >> FX_SHIFT, camy = cam.y >> FX_SHIFT, camz = cam.z >> FX_SHIFT;
    int ccx = (camx & world.mask) >> 4, ccz = (camz & world.mask) >> 4;
    int csy = iclamp(camy >> 4, 0, SEC_Y - 1);
    int R = render_distance;
    int maxR = (world.chunks >> 1) - 1;
    if (R > maxR)
        R = maxR;
    cam_outdoors = (world_light_raw(camx, camy, camz) >> 4) >= 12;
    // exits allowed from the camera's own section
    Section *cs = section_at(ccx, csy, ccz);
    if (camx != cached_cx || camy != cached_cy || camz != cached_cz || cs->dirty || !cs->built ||
        (render_frame_no - cached_dirty_stamp) > 30) {
        cached_exit_mask = camera_exit_mask(camx, camy, camz);
        cached_cx = camx; cached_cy = camy; cached_cz = camz;
        cached_dirty_stamp = render_frame_no;
    }
    int head = 0, tail = 0;
    bfs_queue[tail++] = (BfsNode){ 0, 0, csy, -1, 0, 0 };
    section_at(ccx, csy, ccz)->last_seen = render_frame_no;
    int32_t r_sphere = 14 << 12;
    while (head < tail) {
        stat_bfs = tail;
        BfsNode n = bfs_queue[head++];
        int cx = ccx + n.dcx, cz = ccz + n.dcz;
        Section *s = section_at(cx, n.sy, cz);
        // origin relative to camera (camera position wrapped into the world)
        int32_t rx = ((n.dcx * 16 + ccx * 16) << FX_SHIFT) - (cam.x & ((world.size << FX_SHIFT) - 1));
        int32_t rz = ((n.dcz * 16 + ccz * 16) << FX_SHIFT) - (cam.z & ((world.size << FX_SHIFT) - 1));
        int32_t ry = ((n.sy * 16) << FX_SHIFT) - cam.y;
        bool visible = sphere_visible(rx + (8 << 12), ry + (8 << 12), rz + (8 << 12), r_sphere);
        if (!s->built || s->dirty) {
            if (npending < MAX_VIS) {
                int32_t cxr = (rx + (8 << 12)) >> 8, cyr = (ry + (8 << 12)) >> 8, czr = (rz + (8 << 12)) >> 8;
                pending[npending].idx = s - sections;
                pending[npending].dist = iabs(n.dcx) + iabs(n.dcz) + iabs(n.sy - csy) - (s->built ? 0 : 100) + (visible ? 0 : 3);
                pending[npending].d2 = cxr * cxr + cyr * cyr + czr * czr;
                if (cam_outdoors && s->built && !s->sky_exposed)
                    pending[npending].d2 += (40 * 16) * (40 * 16);
                npending++;
            }
        }
        if (visible && s->built && !s->empty && s->list && nvis < MAX_VIS) {
            VisSec *v = &vis[nvis++];
            v->sec = s;
            v->rx = rx;
            v->ry = ry;
            v->rz = rz;
            // face W (normal -X) can be visible only when camera x < section max x, etc.
            uint8_t dm = 0;
            if (rx + (16 << 12) > 0) dm |= 1 << FACE_W;   // cam x < section max
            if (rx < 0) dm |= 1 << FACE_E;                 // cam x > section min
            if (ry + (16 << 12) > 0) dm |= 1 << FACE_D;
            if (ry < 0) dm |= 1 << FACE_U;
            if (rz + (16 << 12) > 0) dm |= 1 << FACE_N;
            if (rz < 0) dm |= 1 << FACE_S;
            v->dirmask = dm;
            int32_t cxr = (rx + (8 << 12)) >> 8, cyr = (ry + (8 << 12)) >> 8, czr = (rz + (8 << 12)) >> 8;
            v->dist = cxr * cxr + cyr * cyr + czr * czr;
            // When the camera is outdoors, dark underground sections come last
            if (cam_outdoors && !s->sky_exposed)
                v->dist += (40 * 16) * (40 * 16);
        }
        if (!visible && head > 1)
            continue; // don't traverse through invisible sections (except the start)
        uint16_t sv = s->built ? s->vis : 0x7FFF;
        for (int f = 0; f < 6; f++) {
            if (n.dirs & (1 << face_opposite(f)))
                continue;
            if (n.enter >= 0 && !vis_connected(sv, n.enter, f))
                continue;
            if (n.enter < 0 && !(cached_exit_mask & (1 << f)))
                continue;
            int nsy = n.sy + face_dy[f];
            if (nsy < 0 || nsy >= SEC_Y)
                continue;
            int ndx = n.dcx + face_dx[f], ndz = n.dcz + face_dz[f];
            if (iabs(ndx) > R || iabs(ndz) > R)
                continue;
            if (ndx * ndx + ndz * ndz > (R + 1) * (R + 1))
                continue;
            Section *ns = section_at(ccx + ndx, nsy, ccz + ndz);
            if (ns->last_seen == render_frame_no)
                continue;
            ns->last_seen = render_frame_no;
            if (tail < MAX_VIS)
                bfs_queue[tail++] = (BfsNode){ ndx, ndz, nsy, face_opposite(f), n.dirs | (1 << f), 0 };
        }
    }
}

static int cmp_vis(const void *a, const void *b)
{
    int32_t da = ((const VisSec *)a)->dist, db = ((const VisSec *)b)->dist;
    return da < db ? -1 : da > db ? 1 : 0;
}

// Sort visible sections near to far and keep as many as fit the polygon budget
static void apply_budget(void)
{
    qsort(vis, nvis, sizeof(VisSec), cmp_vis);
    int kept = 0;
    budget_cut_d2 = 0x7FFFFFFF;
    for (int i = 0; i < nvis; i++) {
        VisSec *v = &vis[i];
        Section *s = v->sec;
        int32_t rd = v->dist < (40 * 16) * (40 * 16) ? v->dist : v->dist - (40 * 16) * (40 * 16);
        // Unlit faces far away sit in the fog and look like the sky: skip them outdoors
        v->draw_dark = !cam_outdoors || rd < (20 * 16) * (20 * 16);
        int cost = s->nq[SL_SPECIAL] + s->nq[SL_WATER_TOP] + s->nq[SL_WATER_OTHER];
        for (int d = 0; d < 6; d++) {
            if (!(v->dirmask & (1 << d)))
                continue;
            int n = s->nq[d] + (v->draw_dark ? s->nq[SL_DARK_W + d] : 0);
            // if the camera is inside the section's span on that axis, about half the faces face away
            bool straddle = (d == FACE_W || d == FACE_E) ? (v->dirmask & 3) == 3 :
                            (d == FACE_D || d == FACE_U) ? (v->dirmask & 12) == 12 : (v->dirmask & 48) == 48;
            cost += straddle ? n / 2 : n;
        }
        // Faces outside the view frustum are never stored by the hardware:
        // sections around the camera only contribute part of their faces.
        if (rd < (12 * 16) * (12 * 16))
            cost = cost * 2 / 5;
        else if (rd < (22 * 16) * (22 * 16))
            cost = cost * 3 / 4;
        if (poly_budget_used + cost > POLY_BUDGET && kept > 2) {
            if (!budget_hit) {
                budget_cut_d2 = v->dist;
                int32_t d = v->dist;
                if (d >= (40 * 16) * (40 * 16))
                    d -= (40 * 16) * (40 * 16);
                // sqrt of squared 1/16-block units -> fx blocks
                dropped_dist_fx = (int32_t)(sqrt32(d) << 8) - FX(8);
            }
            budget_hit = true;
            continue;
        }
        poly_budget_used += cost;
        s->last_drawn = render_frame_no;
        vis[kept++] = *v;
    }
    nvis = kept;
    // Don't build meshes for sections the budget would not draw anyway: they
    // only waste time and memory. (A little margin lets the cutoff move out.)
    if (budget_hit) {
        int32_t lim = budget_cut_d2 + (budget_cut_d2 >> 2);
        int j = 0;
        for (int i = 0; i < npending; i++)
            if (pending[i].d2 <= lim || pending[i].d2 < (24 * 16) * (24 * 16))
                pending[j++] = pending[i];
        npending = j;
    }
}

static int cmp_pending(const void *a, const void *b)
{
    return ((const Pending *)a)->dist - ((const Pending *)b)->dist;
}

int render_pending_count(void)
{
    return npending;
}

int render_build_pending(int32_t budget_ticks)
{
    if (!npending)
        return 0;
    qsort(pending, npending, sizeof(Pending), cmp_pending);
    int built = 0;
    cpuStartTiming(1);
    for (int i = 0; i < npending; i++) {
        int idx = pending[i].idx;
        int sy = idx % SEC_Y;
        int c = idx / SEC_Y;
        int cx = c % world.chunks, cz = c / world.chunks;
        Section *s = &sections[idx];
        if (s->built && !s->dirty)
            continue;
        mesh_build(cx, sy, cz);
        built++;
        if (budget_ticks > 0 && (int32_t)cpuGetTiming() > budget_ticks)
            break;
        if (budget_ticks <= 0 && built >= 32)
            break;   // loading: rebuild the visible set between batches
    }
    cpuEndTiming();
    // remove built entries
    int j = 0;
    for (int i = 0; i < npending; i++) {
        Section *s = &sections[pending[i].idx];
        if (!s->built || s->dirty)
            pending[j++] = pending[i];
    }
    npending = j;
    stat_builds = built;
    return built;
}

void render_free_far(void)
{
    // Free display lists of sections far outside render distance
    int camx = cam.x >> FX_SHIFT, camz = cam.z >> FX_SHIFT;
    int ccx = (camx & world.mask) >> 4, ccz = (camz & world.mask) >> 4;
    int R = render_distance + 2;
    for (int cz = 0; cz < world.chunks; cz++) {
        for (int cx = 0; cx < world.chunks; cx++) {
            int dx = cx - ccx, dz = cz - ccz;
            int half = world.chunks / 2;
            if (dx > half) dx -= world.chunks;
            if (dx < -half) dx += world.chunks;
            if (dz > half) dz -= world.chunks;
            if (dz < -half) dz += world.chunks;
            if (iabs(dx) <= R && iabs(dz) <= R)
                continue;
            for (int sy = 0; sy < SEC_Y; sy++) {
                Section *s = section_at(cx, sy, cz);
                if (s->list)
                    mesh_free_section(s);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Sky
// ---------------------------------------------------------------------------

static int lerpi(int a, int b, int t, int tmax)
{
    return a + (b - a) * t / tmax;
}

static uint16_t lerp_rgb(int r0, int g0, int b0, int r1, int g1, int b1, int t, int tmax)
{
    return RGB15(lerpi(r0, r1, t, tmax), lerpi(g0, g1, t, tmax), lerpi(b0, b1, t, tmax));
}

static int light_r = 31, light_g = 31, light_b = 31;
static int32_t sky_time;

void sky_update(int32_t time, bool underwater, bool in_lava)
{
    sky_time = time;
    // day factor 0..256
    int f;
    if (time < 12000)
        f = 256;
    else if (time < 13800)
        f = 256 - (time - 12000) * 256 / 1800;
    else if (time < 22200)
        f = 0;
    else
        f = (time - 22200) * 256 / 1800;
    if (time < 1000)
        f = 200 + time * 56 / 1000;
    int lv = 5 + f * 26 / 256;
    sky_daylight = lv;
    light_r = lv;
    light_g = lv;
    light_b = imin(31, lv + (256 - f) * 4 / 256);
    // sky color
    uint16_t day = RGB15(15, 21, 31), night = RGB15(1, 1, 4);
    if (f >= 256)
        sky_color = day;
    else if (f <= 0)
        sky_color = night;
    else {
        // sunset tint around the middle
        int dr = 15, dg = 21, db = 31;
        int sr = 28, sg = 14, sb = 10;
        int t = f;
        if (t > 128)
            sky_color = lerp_rgb(sr, sg, sb, dr, dg, db, t - 128, 128);
        else
            sky_color = lerp_rgb(1, 1, 4, sr, sg, sb, t, 128);
    }
    fog_color = sky_color;
    if (underwater) {
        fog_color = RGB15(2, 5, 14);
        light_r = light_r * 6 / 10;
        light_g = light_g * 8 / 10;
    }
    if (in_lava) {
        fog_color = RGB15(28, 8, 0);
    }
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------

static void load_texmatrix(int dir)
{
    // S/T from local vertex position (1 block = 0.25 units -> 16 texels)
    const int32_t k = 1 << 22;
    m4x4 m;
    memset(&m, 0, sizeof(m));
    switch (dir) {
        case FACE_U: m.m[0] = k; m.m[9] = k; break;
        case FACE_D: m.m[0] = k; m.m[9] = -k; break;
        case FACE_N: m.m[0] = -k; m.m[5] = -k; break;
        case FACE_S: m.m[0] = k; m.m[5] = -k; break;
        case FACE_W: m.m[8] = k; m.m[5] = -k; break;
        default: m.m[8] = -k; m.m[5] = -k; break;
    }
    m.m[15] = 1 << 12;
    glMatrixMode(GL_TEXTURE);
    glLoadMatrix4x4(&m);
    glMatrixMode(GL_MODELVIEW);
}

static void setup_view(void)
{
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glRotateXi(cam.pitch);
    glRotateYi(cam.yaw);
    glScalef32(4 << 12, 4 << 12, 4 << 12);
}

static inline void draw_section_list(VisSec *v, int sl)
{
    int len = v->sec->len[sl];
    if (!len)
        return;
    glPushMatrix();
    glTranslatef32(v->rx >> 2, v->ry >> 2, v->rz >> 2);
    gx_call(v->sec->list + v->sec->off[sl], len);
    glPopMatrix(1);
}

void draw_box(int32_t x0, int32_t y0, int32_t z0, int32_t x1, int32_t y1, int32_t z1)
{
    // coordinates in fx (blocks) relative to current matrix (scaled view: units of 4 blocks)
    // Uses VTX16 with 1/4 scale
#define V16(x, y, z) glVertex3v16((x) >> 2, (y) >> 2, (z) >> 2)
    glBegin(GL_QUADS);
    // top
    V16(x0, y1, z0); V16(x0, y1, z1); V16(x1, y1, z1); V16(x1, y1, z0);
    // bottom
    V16(x0, y0, z0); V16(x1, y0, z0); V16(x1, y0, z1); V16(x0, y0, z1);
    // north (-z)
    V16(x0, y0, z0); V16(x0, y1, z0); V16(x1, y1, z0); V16(x1, y0, z0);
    // south
    V16(x0, y0, z1); V16(x1, y0, z1); V16(x1, y1, z1); V16(x0, y1, z1);
    // west
    V16(x0, y0, z0); V16(x0, y0, z1); V16(x0, y1, z1); V16(x0, y1, z0);
    // east
    V16(x1, y0, z0); V16(x1, y1, z0); V16(x1, y1, z1); V16(x1, y0, z1);
    glEnd();
#undef V16
}

static void draw_sky_objects(void)
{
    // Sun and moon on a circle around the camera, rotating with time
    int32_t ang = (int32_t)((int64_t)sky_time * ANG_FULL / DAY_LENGTH);
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(2));
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glRotateXi(cam.pitch);
    glRotateYi(cam.yaw);
    for (int i = 0; i < 2; i++) {
        int32_t a = -ang + (i ? ANG_FULL / 2 : 0) + ANG_FULL / 4;
        // Below the horizon it would show through gaps where distant terrain isn't drawn
        if (sinLerp(a) < -FX(0.12))
            continue;
        glPushMatrix();
        glRotateZi(a);
        // place at distance along +X axis (east at sunrise)
        int32_t dist = (render_distance * 16) << 10; // quarter scale
        GFX_TEX_FORMAT = gx_texparam16(i ? TEX_MOON : TEX_SUN, TEXGEN_NONE);
        GFX_PAL_FORMAT = 0;
        glColor3b(255, 255, 255);
        int32_t s = (render_distance * 16 / 8 + 3) << 10;
        glBegin(GL_QUADS);
        glTexCoord2t16(0, 0); glVertex3v16(dist >> 2, s >> 2, -s >> 2);
        glTexCoord2t16(0, 256); glVertex3v16(dist >> 2, -s >> 2, -s >> 2);
        glTexCoord2t16(256, 256); glVertex3v16(dist >> 2, -s >> 2, s >> 2);
        glTexCoord2t16(256, 0); glVertex3v16(dist >> 2, s >> 2, s >> 2);
        glEnd();
        glPopMatrix(1);
    }
}

static void draw_clouds(void)
{
    if (sky_daylight < 8)
        return;
    int32_t h = ((WORLD_H + 12) << 12) - cam.y;
    int32_t ext = (render_distance * 16 + 16) << 12;
    // scroll with world position and time
    int32_t offx = (cam.x + (int32_t)(world.ticks * 40)) ;
    int32_t offz = cam.z;
    // texture: 64x64, 1 texel = 1 block -> texcoord units 1/16 texel
    glPolyFmt(POLY_ALPHA(20) | POLY_CULL_NONE | POLY_ID(3) | POLY_FOG);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glRotateXi(cam.pitch);
    glRotateYi(cam.yaw);
    glScalef32(4 << 12, 4 << 12, 4 << 12);
    uint32_t addr = TEX_CLOUD_ADDR;
    GFX_TEX_FORMAT = (addr >> 3) | (1 << 16) | (1 << 17) | (3 << 20) | (3 << 23) | (4 << 26) | (1u << 29);
    GFX_PAL_FORMAT = 0;
    int c = sky_daylight * 8;
    if (c > 255) c = 255;
    glColor3b(c, c, c);
    int32_t s0 = (offx - ext) >> 10, s1 = (offx + ext) >> 10;   // 1 texel per 4 blocks... (texels*16)/... keep scale
    int32_t t0 = (offz - ext) >> 10, t1 = (offz + ext) >> 10;
    glBegin(GL_QUADS);
    glTexCoord2t16(s0 & 0xFFFF, t0 & 0xFFFF); glVertex3v16(-ext >> 2, h >> 2, -ext >> 2);
    glTexCoord2t16(s0 & 0xFFFF, t1 & 0xFFFF); glVertex3v16(-ext >> 2, h >> 2, ext >> 2);
    glTexCoord2t16(s1 & 0xFFFF, t1 & 0xFFFF); glVertex3v16(ext >> 2, h >> 2, ext >> 2);
    glTexCoord2t16(s1 & 0xFFFF, t0 & 0xFFFF); glVertex3v16(ext >> 2, h >> 2, -ext >> 2);
    glEnd();
}

static void draw_target(void)
{
    if (!target_valid)
        return;
    int32_t rx, ry, rz;
    render_rel(target_x << 12, target_y << 12, target_z << 12, &rx, &ry, &rz);
    setup_view();
    glPushMatrix();
    glTranslatef32(rx >> 2, ry >> 2, rz >> 2);
    const int32_t e = 24; // slight expansion (fx)
    // break cracks
    if (target_crack >= 0) {
        glPolyFmt(POLY_ALPHA(31) | POLY_CULL_BACK | POLY_ID(4));
        GFX_TEX_FORMAT = gx_texparam16(TEX_CRACK_0 + iclamp(target_crack, 0, 9), TEXGEN_VERTEX);
        GFX_PAL_FORMAT = 0;
        glColor3b(60, 60, 60);
        for (int d = 0; d < 6; d++) {
            load_texmatrix(d);
            // draw one face per texture matrix
            int32_t x0 = -e, y0 = -e, z0 = -e, x1 = 4096 + e, y1 = 4096 + e, z1 = 4096 + e;
            glTexCoord2t16(0, 0);
#define V16(x, y, z) glVertex3v16((x) >> 2, (y) >> 2, (z) >> 2)
            glBegin(GL_QUADS);
            switch (d) {
                case FACE_U: V16(x0, y1, z0); V16(x0, y1, z1); V16(x1, y1, z1); V16(x1, y1, z0); break;
                case FACE_D: V16(x0, y0, z0); V16(x1, y0, z0); V16(x1, y0, z1); V16(x0, y0, z1); break;
                case FACE_N: V16(x0, y0, z0); V16(x0, y1, z0); V16(x1, y1, z0); V16(x1, y0, z0); break;
                case FACE_S: V16(x0, y0, z1); V16(x1, y0, z1); V16(x1, y1, z1); V16(x0, y1, z1); break;
                case FACE_W: V16(x0, y0, z0); V16(x0, y0, z1); V16(x0, y1, z1); V16(x0, y1, z0); break;
                default: V16(x1, y0, z0); V16(x1, y1, z0); V16(x1, y1, z1); V16(x1, y0, z1); break;
            }
            glEnd();
#undef V16
        }
    }
    // wireframe outline (alpha 0 = wireframe on DS)
    glPolyFmt(POLY_ALPHA(0) | POLY_CULL_NONE | POLY_ID(5));
    GFX_TEX_FORMAT = 0;
    glColor3b(10, 10, 10);
    draw_box(-e, -e, -e, 4096 + e, 4096 + e, 4096 + e);
    glPopMatrix(1);
}

static int32_t fog_end_cur = 0;   // blocks (fx)

void render_frame(void)
{
    sin_yaw = sinLerp(cam.yaw);
    cos_yaw = cosLerp(cam.yaw);
    sin_pitch = sinLerp(cam.pitch);
    cos_pitch = cosLerp(cam.pitch);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspectivef32(degreesToAngle(70), floattof32(256.0 / 192.0), floattof32(NEAR_PLANE),
                      (render_distance * 16 + 24) << 12);

    glLight(0, RGB15(light_r, light_g, light_b), 0, floattov10(-0.99), 0);

    traverse();
    apply_budget();
    PROF_MARK(7);

    // Fog: end where drawn terrain ends (smoothly)
    int32_t target = (render_distance * 16 - 2) << 12;
    if (budget_hit && dropped_dist_fx > 0 && dropped_dist_fx < target)
        target = imax(FX(18), dropped_dist_fx - FX(2));
    if (fog_end_cur == 0)
        fog_end_cur = target;
    fog_end_cur += (target - fog_end_cur) / 6;
    setup_fog_end(fog_end_cur);
    if (cam_underwater_fog) {
        setup_fog_end(FX(14));
    }
    glClearColor((sky_color & 31), (sky_color >> 5) & 31, (sky_color >> 10) & 31, 31);
    glFogColor(fog_color & 31, (fog_color >> 5) & 31, (fog_color >> 10) & 31, 31);
    glFogShift(fog_shift_v);
    glFogOffset(fog_offset_v);
    for (int i = 0; i < 32; i++)
        glFogDensity(i, i * 4 > 127 ? 127 : i * 4);
    glFogDensity(31, 127);

    // Things that must never be dropped by the polygon limit come first
    draw_sky_objects();
    draw_clouds();
    player_render_hand();
    entities_render();
    extern uint16_t prof_ent;
    prof_ent = TIMER_DATA(0);
    draw_target();

    // World: near-to-far bands. If the hardware polygon limit is reached,
    // only the farthest terrain gets dropped.
    setup_view();
    GFX_PAL_FORMAT = 0;
    stat_sections_drawn = nvis;
    stat_budget = poly_budget_used;
    stat_budget_hit = budget_hit;
    const int BAND = 10;
    for (int b0 = 0; b0 < nvis; b0 += BAND) {
        int b1 = imin(nvis, b0 + BAND);
        for (int d = 0; d < 6; d++) {
            bool any = false;
            for (int i = b0; i < b1 && !any; i++)
                if ((vis[i].dirmask & (1 << d)) && (vis[i].sec->len[d] || (vis[i].draw_dark && vis[i].sec->len[SL_DARK_W + d])))
                    any = true;
            if (!any)
                continue;
            load_texmatrix(d);
            glPolyFmt(POLY_ALPHA(31) | POLY_CULL_BACK | POLY_FORMAT_LIGHT0 | POLY_FOG | POLY_ID(0));
            for (int i = b0; i < b1; i++) {
                if (vis[i].dirmask & (1 << d)) {
                    draw_section_list(&vis[i], d);
                    if (vis[i].draw_dark)
                        draw_section_list(&vis[i], SL_DARK_W + d);
                }
            }
        }
        // Cutout specials (plants, torches...)
        glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_FORMAT_LIGHT0 | POLY_FOG | POLY_ID(0));
        for (int i = b0; i < b1; i++)
            draw_section_list(&vis[i], SL_SPECIAL);
        // Translucent water (hardware sorts translucent polygons after opaque ones)
        bool water = false;
        for (int i = b0; i < b1 && !water; i++)
            if (vis[i].sec->len[SL_WATER_TOP] || vis[i].sec->len[SL_WATER_OTHER])
                water = true;
        if (water) {
            load_texmatrix(FACE_U);
            glPolyFmt(POLY_ALPHA(18) | POLY_CULL_NONE | POLY_FORMAT_LIGHT0 | POLY_FOG | POLY_ID(1));
            for (int i = b0; i < b1; i++) {
                draw_section_list(&vis[i], SL_WATER_TOP);
                draw_section_list(&vis[i], SL_WATER_OTHER);
            }
        }
    }

    stat_polys = GFX_POLYGON_RAM_USAGE;
    stat_verts = GFX_VERTEX_RAM_USAGE;
    glFlush(0);
}

void render_begin_entities(void)
{
    setup_view();
}

void render_end_entities(void)
{
}

int render_debug_mask(void) { return cached_exit_mask; }
