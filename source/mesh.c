// Section mesher: greedy meshing of cube faces into packed GX display lists.
#include <stdlib.h>
#include "mesh.h"
#include "render.h"
#include "world.h"
#include "blocks.h"
#include "gx.h"
#include "assets_gen.h"

Section *sections;
int sections_count;

uint16_t light_ambient[6][16];
uint16_t light_emission[6][16];

// Scratch buffers
#define LS 18
static uint8_t loc_id[LS * LS * LS];
static uint8_t loc_li[LS * LS * LS];
// y is the fastest axis so a world column copies straight in
#define LI(x, y, z) (((((z) + 1) * LS + ((x) + 1)) * LS) + ((y) + 1))
#define RI(x, z) (((z) + 1) * LS + ((x) + 1))   // row (column) index, bit (y + 1)
static uint32_t rcube[LS * LS], ropaq[LS * LS], rspec[LS * LS];
DTCM_BSS static uint16_t fbd[256];      // per-direction face bits for interior columns (index z * 16 + x, bit y)

#define MAX_QUADS 4096
typedef struct {
    uint32_t key;
    uint32_t v[4];
} Quad;
static Quad quads[MAX_QUADS];
static int nquads;

static uint8_t cube_tab[256], opaque_tab[256], spec_tab[256], cls_tab[256];
static bool tabs_ready;
// Neighbor offset in the local cache per direction (W, E, D, U, N, S)
static const int16_t dir_noff[6] = { -LS, LS, -1, 1, -LS * LS, LS * LS };

static void mesh_build_tables(void)
{
    for (int i = 0; i < 256; i++) {
        cube_tab[i] = blocks[i].shape == SHAPE_CUBE;
        opaque_tab[i] = (blocks[i].flags & BF_OPAQUE) != 0;
        spec_tab[i] = blocks[i].shape != SHAPE_CUBE && blocks[i].shape != SHAPE_NONE;
        cls_tab[i] = cube_tab[i] | (opaque_tab[i] << 1) | (spec_tab[i] << 2);
    }
    tabs_ready = true;
}


#define SCRATCH_WORDS 24000
static uint32_t scratch[SCRATCH_WORDS];

DTCM_BSS static uint32_t mask[16 * 16];

static const uint8_t face_shade[6] = { 60, 60, 50, 100, 80, 80 }; // percent

void mesh_init_light_tables(void)
{
    for (int d = 0; d < 6; d++) {
        for (int L = 0; L < 16; L++) {
            // Brightness curve (mix of Minecraft-like curve and linear)
            int fp = (15 - L) * 4096 / 15;                 // 1 - L/15
            int mc = (4096 - fp) * 4096 / (fp * 3 + 4096); // (1-f')/(3f'+1)
            int lin = L * 4096 / 15;
            int b = (mc * 62 + lin * 38) / 100;            // 0..4096
            int v = (b * 31 * face_shade[d]) / (4096 * 100);
            if (v > 31)
                v = 31;
            light_ambient[d][L] = RGB15(v, v, v);
            int r = (b * 31 * face_shade[d]) / (4096 * 100);
            int g = r * 86 / 100;
            int bl = r * 66 / 100;
            if (r < 2) r = 2;
            if (g < 2) g = 2;
            if (bl < 2) bl = 2;
            light_emission[d][L] = RGB15(r, g, bl);
        }
    }
}

int section_index(int cx, int sy, int cz)
{
    cx &= world.chunks - 1;
    cz &= world.chunks - 1;
    return (cz * world.chunks + cx) * SEC_Y + sy;
}

uint32_t mesh_bytes, mesh_budget;
static void mesh_build_inner(int cx, int sy, int cz);
extern uint32_t mesh_prof[8];

// Rough measure of free heap: grab 32 KB blocks until malloc fails
static uint32_t probe_free_heap(void)
{
    enum { CHUNK = 32 * 1024, MAXC = 512 };
    static void *blocks_tmp[MAXC];
    int n = 0;
    while (n < MAXC && (blocks_tmp[n] = malloc(CHUNK)) != NULL)
        n++;
    for (int i = 0; i < n; i++)
        free(blocks_tmp[i]);
    return (uint32_t)n * CHUNK;
}

bool mesh_init(void)
{
    mesh_shutdown();
    sections_count = world.chunks * world.chunks * SEC_Y;
    sections = calloc(sections_count, sizeof(Section));
    if (!sections)
        return false;
    for (int i = 0; i < sections_count; i++)
        sections[i].dirty = 1;
    mesh_init_light_tables();
    // Keep a reserve for saving (FAT buffers) and other allocations
    uint32_t avail = probe_free_heap();
    uint32_t reserve = 192 * 1024;
    mesh_budget = avail > reserve + 128 * 1024 ? avail - reserve : 128 * 1024;
    if (mesh_budget > 6 * 1024 * 1024)
        mesh_budget = 6 * 1024 * 1024;
    mesh_bytes = 0;
    return true;
}

void mesh_free_section(Section *s)
{
    if (s->list)
        mesh_bytes -= s->words * 4;
    free(s->list);
    s->list = NULL;
    s->words = 0;
    s->built = 0;
    s->dirty = 1;
}

void mesh_shutdown(void)
{
    if (sections) {
        for (int i = 0; i < sections_count; i++)
            free(sections[i].list);
        free(sections);
    }
    sections = NULL;
    sections_count = 0;
    mesh_bytes = 0;
}

// Free the display lists least likely to be needed soon: sections not seen
// recently and far from the camera. Returns false if nothing could be freed.
extern uint32_t render_frame_no;
static bool mesh_evict_one(Section *keep)
{
    int camcx = ((cam.x >> FX_SHIFT) & world.mask) >> 4;
    int camcz = ((cam.z >> FX_SHIFT) & world.mask) >> 4;
    int half = world.chunks / 2;
    Section *best = NULL;
    int best_score = -1;
    for (int cz = 0; cz < world.chunks; cz++) {
        int dz = cz - camcz;
        if (dz > half) dz -= world.chunks;
        if (dz < -half) dz += world.chunks;
        for (int cx = 0; cx < world.chunks; cx++) {
            int dx = cx - camcx;
            if (dx > half) dx -= world.chunks;
            if (dx < -half) dx += world.chunks;
            int d2 = dx * dx + dz * dz;
            if (d2 <= 2)
                continue;   // never evict the camera's neighborhood
            for (int sy = 0; sy < SEC_Y; sy++) {
                Section *s = section_at(cx, sy, cz);
                if (!s->list || s == keep)
                    continue;
                bool recent = (render_frame_no - s->last_drawn) < 4;
                int score = (recent ? 0 : (1 << 20)) + d2 * 8 + s->words / 512;
                if (score > best_score) {
                    best_score = score;
                    best = s;
                }
            }
        }
    }
    if (!best)
        return false;
    mesh_free_section(best);
    return true;
}

void mesh_invalidate_all(void)
{
    for (int i = 0; i < sections_count; i++)
        sections[i].dirty = 1;
}

void mesh_mark_section(int cx, int sy, int cz)
{
    if (sy < 0 || sy >= SEC_Y || !sections)
        return;
    section_at(cx, sy, cz)->dirty = 1;
}

void mesh_mark_block(int x, int y, int z)
{
    if (!sections || y < 0 || y >= WORLD_H)
        return;
    int cx = (x & world.mask) >> 4, cz = (z & world.mask) >> 4, sy = y >> 4;
    mesh_mark_section(cx, sy, cz);
    int lx = x & 15, ly = y & 15, lz = z & 15;
    if (lx == 0) mesh_mark_section(cx - 1, sy, cz);
    if (lx == 15) mesh_mark_section(cx + 1, sy, cz);
    if (lz == 0) mesh_mark_section(cx, sy, cz - 1);
    if (lz == 15) mesh_mark_section(cx, sy, cz + 1);
    if (ly == 0) mesh_mark_section(cx, sy - 1, cz);
    if (ly == 15) mesh_mark_section(cx, sy + 1, cz);
}

// ---------------------------------------------------------------------------
// Gather
// ---------------------------------------------------------------------------

ITCM_CODE ARM_CODE static void gather(int cx, int sy, int cz)
{
    int bx = cx * 16, by = sy * 16, bz = cz * 16;
    int y0 = by - 1;
    for (int z = -1; z <= 16; z++) {
        for (int x = -1; x <= 16; x++) {
            int base = widx(bx + x, 0, bz + z);
            const uint8_t *col = &world.blk[base];
            const uint8_t *lcol = &world.light[base];
            int li = LI(x, -1, z);
            uint8_t *d = &loc_id[li], *dl = &loc_li[li];
            int a = 0, b = LS;           // local range copied from the world
            if (y0 < 0) {
                d[0] = B_BEDROCK;
                dl[0] = 0;
                a = 1;
            }
            if (y0 + LS > WORLD_H) {
                d[LS - 1] = B_AIR;
                dl[LS - 1] = 0xF0;
                b = LS - 1;
            }
            for (int k = a; k < b; k++) {
                d[k] = col[y0 + k];
                dl[k] = lcol[y0 + k];
            }
            // column bitmasks
            uint32_t c = 0, o = 0, sp = 0;
            for (int k = 0; k < LS; k++) {
                uint32_t f = cls_tab[d[k]];
                c |= (f & 1) << k;
                o |= ((f >> 1) & 1) << k;
                sp |= (f >> 2) << k;
            }
            int r = RI(x, z);
            rcube[r] = c;
            ropaq[r] = o;
            rspec[r] = sp;
        }
    }
}

// ---------------------------------------------------------------------------
// Visibility (face connectivity through non-opaque cells)
// ---------------------------------------------------------------------------

static uint16_t vstack[4096];
static uint8_t vflag[LS * LS * LS];     // 0 open, 1 closed/visited, 2 + face for the border
static uint8_t vtemplate[LS * LS * LS];
static bool vtemplate_ready;

static void build_vtemplate(void)
{
    for (int z = -1; z <= 16; z++)
        for (int x = -1; x <= 16; x++)
            for (int y = -1; y <= 16; y++) {
                int v = 0;
                if (x < 0) v = 2 + FACE_W;
                else if (x > 15) v = 2 + FACE_E;
                else if (y < 0) v = 2 + FACE_D;
                else if (y > 15) v = 2 + FACE_U;
                else if (z < 0) v = 2 + FACE_N;
                else if (z > 15) v = 2 + FACE_S;
                vtemplate[LI(x, y, z)] = v;
            }
    vtemplate_ready = true;
}

#define INTERIOR_BITS 0x1FFFEu

ITCM_CODE ARM_CODE static uint16_t compute_vis(bool *solid)
{
    // Fast paths: no opaque cells at all, or nothing but opaque cells
    int nop = 0;
    for (int z = 0; z < 16; z++)
        for (int x = 0; x < 16; x++)
            nop += __builtin_popcount(ropaq[RI(x, z)] & INTERIOR_BITS);
    if (nop == 0) {
        *solid = false;
        return 0x7FFF;
    }
    if (nop == 4096) {
        *solid = true;
        return 0;
    }
    if (!vtemplate_ready)
        build_vtemplate();
    memcpy(vflag, vtemplate, sizeof(vflag));
    for (int z = 0; z < 16; z++)
        for (int x = 0; x < 16; x++) {
            uint32_t bits = ropaq[RI(x, z)] & INTERIOR_BITS;
            uint8_t *col = &vflag[LI(x, -1, z)];
            while (bits) {
                int b = __builtin_ctz(bits);
                bits &= bits - 1;
                col[b] = 1;
            }
        }
    static const uint8_t base[6] = { 0, 5, 9, 12, 14, 15 };
    uint16_t vis = 0;
    for (int z = 0; z < 16; z++)
        for (int x = 0; x < 16; x++) {
            int ci = LI(x, -1, z);
            for (int b = 1; b <= 16; b++) {
                int start = ci + b;
                if (vflag[start])
                    continue;
                int sp = 0;
                vstack[sp++] = start;
                vflag[start] = 1;
                uint32_t faces = 0;
                while (sp) {
                    int c = vstack[--sp];
                    int n, v;
#define VSTEP(off) n = c + (off); v = vflag[n]; if (!v) { vflag[n] = 1; vstack[sp++] = n; } else if (v >= 2) faces |= 1u << (v - 2);
                    VSTEP(1) VSTEP(-1) VSTEP(LS) VSTEP(-LS) VSTEP(LS * LS) VSTEP(-LS * LS)
#undef VSTEP
                }
                for (int a = 0; a < 6; a++) {
                    if (!(faces & (1 << a)))
                        continue;
                    for (int bb = a + 1; bb < 6; bb++)
                        if (faces & (1 << bb))
                            vis |= 1 << (base[a] + (bb - a - 1));
                }
            }
        }
    *solid = false;
    return vis;
}

// ---------------------------------------------------------------------------
// Quad emission helpers
// ---------------------------------------------------------------------------


static inline void add_quad(uint32_t key, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    if (nquads >= MAX_QUADS)
        return;
    Quad *q = &quads[nquads++];
    q->key = key;
    q->v[0] = a; q->v[1] = b; q->v[2] = c; q->v[3] = d;
}

// key layout: bits 0-7 light, 8-15 texture, 16-20 height (16ths), 24 = valid, 31 = dark face
#define KEY_DARK (1u << 31)
static inline uint32_t face_light_key(int li)
{
    int sky = li >> 4, blk = li & 15;
    if (sky <= 1 && blk == 0)
        return KEY_DARK | li;
    if (sky >= 13)
        sky = 15;   // merge the brightest levels for fewer quads
    return (sky << 4) | blk;
}
#define MESH_LIGHT_KEY(l) face_light_key(l)
#define KEY(tex, light, h) ((1u << 24) | ((uint32_t)(h) << 16) | ((uint32_t)(tex) << 8) | (light))

// Group quads by (dark, texture, light) with a two-pass counting sort.
// Quads are moved through a small index permutation into qtmp.
static uint16_t qidx_a[MAX_QUADS], qidx_b[MAX_QUADS];
DTCM_BSS static uint16_t qcount[512];

ITCM_CODE ARM_CODE static void sort_quads(void)
{
    int n = nquads;
    if (n < 2)
        return;
    // pass 1: by light byte
    for (int b = 0; b < 256; b++) qcount[b] = 0;
    for (int i = 0; i < n; i++)
        qcount[quads[i].key & 0xFF]++;
    int sum = 0;
    for (int b = 0; b < 256; b++) {
        int c = qcount[b];
        qcount[b] = sum;
        sum += c;
    }
    for (int i = 0; i < n; i++)
        qidx_a[qcount[quads[i].key & 0xFF]++] = i;
    // pass 2: by texture byte, dark faces last
    memset(qcount, 0, 512 * sizeof(uint16_t));
    for (int i = 0; i < n; i++) {
        uint32_t k = quads[i].key;
        qcount[((k >> 8) & 0xFF) | ((k >> 23) & 0x100)]++;
    }
    sum = 0;
    for (int b = 0; b < 512; b++) {
        int c = qcount[b];
        qcount[b] = sum;
        sum += c;
    }
    for (int j = 0; j < n; j++) {
        int i = qidx_a[j];
        uint32_t k = quads[i].key;
        qidx_b[qcount[((k >> 8) & 0xFF) | ((k >> 23) & 0x100)]++] = i;
    }
    // apply the permutation in place (cycle walk)
    for (int j = 0; j < n; j++)
        qidx_a[qidx_b[j]] = j;          // qidx_a[i] = destination of quad i
    for (int i = 0; i < n; i++) {
        while (qidx_a[i] != i) {
            int dst = qidx_a[i];
            Quad t = quads[dst];
            quads[dst] = quads[i];
            quads[i] = t;
            uint16_t td = qidx_a[dst];
            qidx_a[dst] = dst;
            qidx_a[i] = td;
        }
    }
}

ITCM_CODE ARM_CODE static void emit_texgen_quads_range(GxList *l, int dir, int first, int last)
{
    if (first >= last)
        return;
    gxl_cmd1(l, GXC_TEXCOORD, 0);
    int cur_tex = -1, cur_light = -1;
    for (int i = first; i < last; i++) {
        Quad *q = &quads[i];
        int tex = (q->key >> 8) & 0xFF;
        int light = q->key & 0xFF;
        if (tex != cur_tex) {
            if (cur_tex >= 0)
                gxl_cmd0(l, GXC_END);
            gxl_cmd1(l, GXC_TEXIMAGE, gx_texparam16(tex, TEXGEN_VERTEX));
            gxl_cmd1(l, GXC_BEGIN, GL_QUADS);
            cur_tex = tex;
        }
        if (light != cur_light) {
            gxl_cmd1(l, GXC_DIF_AMB, (uint32_t)light_ambient[dir][light >> 4] << 16);
            gxl_cmd1(l, GXC_SPE_EMI, (uint32_t)light_emission[dir][light & 15] << 16);
            gxl_cmd1(l, GXC_NORMAL, 0);
            cur_light = light;
        }
        // four vertices as one aligned packed group
        if (l->nc)
            gxl_flush(l);
        if (l->pos + 5 <= l->cap) {
            uint32_t *w = l->buf + l->pos;
            w[0] = GXC_VTX10 * 0x01010101u;
            w[1] = q->v[0];
            w[2] = q->v[1];
            w[3] = q->v[2];
            w[4] = q->v[3];
            l->pos += 5;
        } else {
            l->overflow = true;
        }
    }
    gxl_cmd0(l, GXC_END);
    gxl_flush(l);
}

static void emit_texgen_quads(GxList *l, int dir)
{
    if (!nquads)
        return;
    sort_quads();
    emit_texgen_quads_range(l, dir, 0, nquads);
}

// Quad vertices for a face in direction d at slice plane p (raw units),
// spanning u0..u1, v0..v1 (raw units).
ITCM_CODE ARM_CODE static void face_quad(uint32_t key, int d, int p, int u0, int u1, int v0, int v1)
{
    switch (d) {
        case FACE_U:
            add_quad(key, GX_V10(u0, p, v0), GX_V10(u0, p, v1), GX_V10(u1, p, v1), GX_V10(u1, p, v0));
            break;
        case FACE_D:
            add_quad(key, GX_V10(u0, p, v0), GX_V10(u1, p, v0), GX_V10(u1, p, v1), GX_V10(u0, p, v1));
            break;
        case FACE_N:
            add_quad(key, GX_V10(u0, v0, p), GX_V10(u0, v1, p), GX_V10(u1, v1, p), GX_V10(u1, v0, p));
            break;
        case FACE_S:
            add_quad(key, GX_V10(u0, v0, p), GX_V10(u1, v0, p), GX_V10(u1, v1, p), GX_V10(u0, v1, p));
            break;
        case FACE_W:
            add_quad(key, GX_V10(p, v0, u0), GX_V10(p, v0, u1), GX_V10(p, v1, u1), GX_V10(p, v1, u0));
            break;
        case FACE_E:
            add_quad(key, GX_V10(p, v0, u0), GX_V10(p, v1, u0), GX_V10(p, v1, u1), GX_V10(p, v0, u1));
            break;
    }
}

ITCM_CODE ARM_CODE static void greedy_slice(int d, int s, int plane_offset_default)
{
    for (int v = 0; v < 16; v++) {
        for (int u = 0; u < 16; u++) {
            uint32_t k = mask[v * 16 + u];
            if (!k)
                continue;
            int w = 1;
            while (u + w < 16 && mask[v * 16 + u + w] == k)
                w++;
            int h = 1;
            while (v + h < 16) {
                bool ok = true;
                for (int i = 0; i < w; i++)
                    if (mask[(v + h) * 16 + u + i] != k) {
                        ok = false;
                        break;
                    }
                if (!ok)
                    break;
                h++;
            }
            for (int j = 0; j < h; j++)
                for (int i = 0; i < w; i++)
                    mask[(v + j) * 16 + u + i] = 0;
            int ph = (k >> 16) & 0x1F; // liquid surface height
            int p;
            if (ph)
                p = s * 16 + ph;
            else
                p = (s + plane_offset_default) * 16;
            face_quad(k, d, p, u * 16, (u + w) * 16, v * 16, (v + h) * 16);
        }
    }
}

static inline bool cube_face_visible(int id, int nid)
{
    const BlockDef *nb = &blocks[nid];
    if (nb->flags & BF_OPAQUE)
        return false;
    if (nid == id)
        return false; // glass next to glass etc.
    return true;
}

// ---------------------------------------------------------------------------
// Special shapes (explicit texture coordinates)
// ---------------------------------------------------------------------------

typedef struct {
    GxList *l;
    int tex;
    int mat;
    bool in_begin;
} SpecState;

static void sp_tex(SpecState *st, int tex)
{
    if (st->tex == tex)
        return;
    if (st->in_begin)
        gxl_cmd0(st->l, GXC_END);
    gxl_cmd1(st->l, GXC_TEXIMAGE, gx_texparam16(tex, TEXGEN_NONE));
    gxl_cmd1(st->l, GXC_BEGIN, GL_QUADS);
    st->in_begin = true;
    st->tex = tex;
}

static void sp_mat(SpecState *st, int dir, int light)
{
    int m = (dir << 8) | light;
    if (st->mat == m)
        return;
    gxl_cmd1(st->l, GXC_DIF_AMB, (uint32_t)light_ambient[dir][light >> 4] << 16);
    gxl_cmd1(st->l, GXC_SPE_EMI, (uint32_t)light_emission[dir][light & 15] << 16);
    gxl_cmd1(st->l, GXC_NORMAL, 0);
    st->mat = m;
}

// vertex with texcoord (s,t in texels)
static inline void sp_vtx(SpecState *st, int x, int y, int z, int s, int t)
{
    gxl_cmd1(st->l, GXC_TEXCOORD, GX_TC(s * 16, t * 16));
    gxl_cmd1(st->l, GXC_VTX10, GX_V10(x, y, z));
}

static void sp_quad(SpecState *st, const int v[4][3], const int tc[4][2])
{
    for (int i = 0; i < 4; i++)
        sp_vtx(st, v[i][0], v[i][1], v[i][2], tc[i][0], tc[i][1]);
}

// Axis aligned box face; coordinates in raw units relative to section origin
static void sp_box_face(SpecState *st, int f, int x0, int y0, int z0, int x1, int y1, int z1, int tex, int light, int rot)
{
    sp_tex(st, tex);
    sp_mat(st, f, light);
    // local fractional coordinates for texcoords
    int fx0 = x0 & 15, fx1 = fx0 + (x1 - x0);
    int fz0 = z0 & 15, fz1 = fz0 + (z1 - z0);
    int fy0 = y0 & 15, fy1 = fy0 + (y1 - y0);
    int v[4][3], tc[4][2];
    switch (f) {
        case FACE_U: {
            int vv[4][3] = { { x0, y1, z0 }, { x0, y1, z1 }, { x1, y1, z1 }, { x1, y1, z0 } };
            int tt[4][2] = { { fx0, fz0 }, { fx0, fz1 }, { fx1, fz1 }, { fx1, fz0 } };
            // rotate texcoords by rot quarter turns
            for (int i = 0; i < 4; i++) {
                memcpy(v[i], vv[i], sizeof(v[i]));
                int j = (i + rot) & 3;
                tc[i][0] = tt[j][0];
                tc[i][1] = tt[j][1];
            }
            break;
        }
        case FACE_D: {
            int vv[4][3] = { { x0, y0, z0 }, { x1, y0, z0 }, { x1, y0, z1 }, { x0, y0, z1 } };
            int tt[4][2] = { { fx0, fz0 }, { fx1, fz0 }, { fx1, fz1 }, { fx0, fz1 } };
            memcpy(v, vv, sizeof(v)); memcpy(tc, tt, sizeof(tc));
            break;
        }
        case FACE_N: {
            int vv[4][3] = { { x0, y0, z0 }, { x0, y1, z0 }, { x1, y1, z0 }, { x1, y0, z0 } };
            int tt[4][2] = { { 16 - fx0, 16 - fy0 }, { 16 - fx0, 16 - fy1 }, { 16 - fx1, 16 - fy1 }, { 16 - fx1, 16 - fy0 } };
            memcpy(v, vv, sizeof(v)); memcpy(tc, tt, sizeof(tc));
            break;
        }
        case FACE_S: {
            int vv[4][3] = { { x0, y0, z1 }, { x1, y0, z1 }, { x1, y1, z1 }, { x0, y1, z1 } };
            int tt[4][2] = { { fx0, 16 - fy0 }, { fx1, 16 - fy0 }, { fx1, 16 - fy1 }, { fx0, 16 - fy1 } };
            memcpy(v, vv, sizeof(v)); memcpy(tc, tt, sizeof(tc));
            break;
        }
        case FACE_W: {
            int vv[4][3] = { { x0, y0, z0 }, { x0, y0, z1 }, { x0, y1, z1 }, { x0, y1, z0 } };
            int tt[4][2] = { { fz0, 16 - fy0 }, { fz1, 16 - fy0 }, { fz1, 16 - fy1 }, { fz0, 16 - fy1 } };
            memcpy(v, vv, sizeof(v)); memcpy(tc, tt, sizeof(tc));
            break;
        }
        default: { // FACE_E
            int vv[4][3] = { { x1, y0, z0 }, { x1, y1, z0 }, { x1, y1, z1 }, { x1, y0, z1 } };
            int tt[4][2] = { { 16 - fz0, 16 - fy0 }, { 16 - fz0, 16 - fy1 }, { 16 - fz1, 16 - fy1 }, { 16 - fz1, 16 - fy0 } };
            memcpy(v, vv, sizeof(v)); memcpy(tc, tt, sizeof(tc));
            break;
        }
    }
    sp_quad(st, v, tc);
}

static void sp_box(SpecState *st, const BlockDef *b, int x0, int y0, int z0, int x1, int y1, int z1,
                   int light, uint8_t skip_mask, int top_rot)
{
    for (int f = 0; f < 6; f++) {
        if (skip_mask & (1 << f))
            continue;
        sp_box_face(st, f, x0, y0, z0, x1, y1, z1, b->tex[f], light, f == FACE_U ? top_rot : 0);
    }
}

static void sp_cross(SpecState *st, int tex, int bx, int by, int bz, int light, int inset, int height)
{
    sp_tex(st, tex);
    sp_mat(st, FACE_U, light);
    int a = inset, b = 16 - inset;
    int top = 16 - height;
    int v1[4][3] = { { bx + a, by, bz + a }, { bx + b, by, bz + b }, { bx + b, by + height, bz + b }, { bx + a, by + height, bz + a } };
    int v2[4][3] = { { bx + b, by, bz + a }, { bx + a, by, bz + b }, { bx + a, by + height, bz + b }, { bx + b, by + height, bz + a } };
    int tc[4][2] = { { 0, 16 }, { 16, 16 }, { 16, top }, { 0, top } };
    sp_quad(st, v1, tc);
    sp_quad(st, v2, tc);
}

static void sp_plane_x(SpecState *st, int tex, int x, int by, int bz, int light)
{
    // vertical plane at constant x spanning z 0..16
    sp_tex(st, tex);
    sp_mat(st, FACE_E, light);
    int v[4][3] = { { x, by, bz }, { x, by, bz + 16 }, { x, by + 16, bz + 16 }, { x, by + 16, bz } };
    int tc[4][2] = { { 0, 16 }, { 16, 16 }, { 16, 0 }, { 0, 0 } };
    sp_quad(st, v, tc);
}

static void sp_plane_z(SpecState *st, int tex, int z, int bx, int by, int light)
{
    sp_tex(st, tex);
    sp_mat(st, FACE_N, light);
    int v[4][3] = { { bx, by, z }, { bx + 16, by, z }, { bx + 16, by + 16, z }, { bx, by + 16, z } };
    int tc[4][2] = { { 0, 16 }, { 16, 16 }, { 16, 0 }, { 0, 0 } };
    sp_quad(st, v, tc);
}

static void sp_torch(SpecState *st, int id, int bx, int by, int bz, int light)
{
    sp_tex(st, TEX_TORCH);
    sp_mat(st, FACE_U, light);
    int lean_x = 0, lean_z = 0;  // offset toward wall
    int lift = 0;
    if (is_wall_torch(id)) {
        int f = id - B_TORCH_N;           // facing away from wall
        static const int8_t fdx[4] = { 0, 1, 0, -1 }, fdz[4] = { -1, 0, 1, 0 };
        lean_x = -fdx[f];
        lean_z = -fdz[f];
        lift = 4;
    }
    // Bottom shifted 6 toward wall, top shifted 2 toward wall (lean outward)
    int bxo = lean_x * 6, bzo = lean_z * 6;
    int txo = lean_x * 2, tzo = lean_z * 2;
    int y0 = by + lift, y1 = by + lift + 16;
    int cx = bx + 8, cz = bz + 8;
    int va[4][3] = { { cx - 8 + bxo, y0, cz + bzo }, { cx + 8 + bxo, y0, cz + bzo }, { cx + 8 + txo, y1, cz + tzo }, { cx - 8 + txo, y1, cz + tzo } };
    int vb[4][3] = { { cx + bxo, y0, cz - 8 + bzo }, { cx + bxo, y0, cz + 8 + bzo }, { cx + txo, y1, cz + 8 + tzo }, { cx + txo, y1, cz - 8 + tzo } };
    int tc[4][2] = { { 0, 16 }, { 16, 16 }, { 16, 0 }, { 0, 0 } };
    sp_quad(st, va, tc);
    sp_quad(st, vb, tc);
}

static void emit_liquid_side(SpecState *st, int id, int f, int bx, int by, int bz, int h, int light)
{
    // side face of liquid cell with surface height h (raw units)
    int x0 = bx, x1 = bx + 16, z0 = bz, z1 = bz + 16, y0 = by, y1 = by + h;
    sp_box_face(st, f, x0, y0, z0, x1, y1, z1, blocks[id].tex[f], light, 0);
}

// ---------------------------------------------------------------------------
// Build
// ---------------------------------------------------------------------------

uint32_t mesh_build_ticks, mesh_build_count;
uint32_t mesh_prof[8];

ARM_CODE void mesh_build(int cx, int sy, int cz)
{
    uint16_t t0 = TIMER_DATA(0);
    mesh_build_inner(cx, sy, cz);
    mesh_build_ticks += (uint16_t)(TIMER_DATA(0) - t0);
    mesh_build_count++;
}

ITCM_CODE ARM_CODE static void mesh_build_inner(int cx, int sy, int cz)
{
    Section *sec = section_at(cx, sy, cz);
    if (!tabs_ready)
        mesh_build_tables();
    uint16_t pt = TIMER_DATA(0);
#define MPROF(i) do { uint16_t _n = TIMER_DATA(0); mesh_prof[i] += (uint16_t)(_n - pt); pt = _n; } while (0)
    gather(cx, sy, cz);
    MPROF(0);

    // Quick empty check
    bool any = false;
    for (int z = 0; z < 16 && !any; z++)
        for (int x = 0; x < 16; x++) {
            int r = RI(x, z);
            if ((rcube[r] | rspec[r]) & INTERIOR_BITS) {
                any = true;
                break;
            }
        }
    if (sec->list)
        mesh_bytes -= sec->words * 4;
    free(sec->list);
    sec->list = NULL;
    sec->words = 0;
    memset(sec->off, 0, sizeof(sec->off));
    memset(sec->len, 0, sizeof(sec->len));
    memset(sec->nq, 0, sizeof(sec->nq));
    sec->quads = 0;
    sec->dirty = 0;
    sec->built = 1;
    if (!any) {
        sec->empty = 1;
        sec->solid = 0;
        sec->sky_exposed = 1;
        sec->vis = 0x7FFF;
        return;
    }
    sec->empty = 0;
    bool solid;
    sec->vis = compute_vis(&solid);
    MPROF(1);
    sec->solid = solid;
    sec->sky_exposed = 0;
    for (int z = 15; z >= 0 && !sec->sky_exposed; z--)
        for (int x = 0; x < 16 && !sec->sky_exposed; x++) {
            uint32_t open = ~ropaq[RI(x, z)] & INTERIOR_BITS;
            const uint8_t *lc = &loc_li[LI(x, -1, z)];
            while (open) {
                int b = 31 - __builtin_clz(open);   // top first: likely lit
                open &= ~(1u << b);
                if ((lc[b] >> 4) > 2) {
                    sec->sky_exposed = 1;
                    break;
                }
            }
        }

    MPROF(2);
    int pos = 0;
    int total_quads = 0;
    GxList gl;

    // ---- Opaque directional lists ----
    memset(mask, 0, sizeof(mask));
    for (int d = 0; d < 6; d++) {
        nquads = 0;
        const int no = dir_noff[d];
        int plane_off = (d & 1) ? 1 : 0; // E, U, S are positive
        // face bits per interior column: cube cells whose neighbor is not opaque
        uint32_t any_all = 0;
        uint16_t any_x[16], any_z[16];
        memset(any_x, 0, sizeof(any_x));
        memset(any_z, 0, sizeof(any_z));
        for (int z = 0; z < 16; z++) {
            for (int x = 0; x < 16; x++) {
                int r = RI(x, z);
                uint32_t nb;
                switch (d) {
                    case FACE_W: nb = ropaq[r - 1]; break;
                    case FACE_E: nb = ropaq[r + 1]; break;
                    case FACE_D: nb = ropaq[r] << 1; break;
                    case FACE_U: nb = ropaq[r] >> 1; break;
                    case FACE_N: nb = ropaq[r - LS]; break;
                    default: nb = ropaq[r + LS]; break;
                }
                uint32_t fb = (rcube[r] & ~nb & INTERIOR_BITS) >> 1;
                if (d == FACE_U && (rspec[r] & INTERIOR_BITS)) {
                    // lava surfaces are part of the opaque up list
                    uint32_t sp = (rspec[r] & INTERIOR_BITS) >> 1;
                    const uint8_t *col = &loc_id[LI(x, 0, z)];
                    while (sp) {
                        int y = __builtin_ctz(sp);
                        sp &= sp - 1;
                        int id = col[y];
                        if (is_lava(id) && !is_lava(col[y + 1]) && !opaque_tab[col[y + 1]])
                            fb |= 1u << y;
                    }
                }
                fbd[z * 16 + x] = fb;
                any_all |= fb;
                any_x[x] |= fb;
                any_z[z] |= fb;
            }
        }
        if (any_all) {
            for (int s = 0; s < 16; s++) {
                bool anyface = false;
                if (d == FACE_W || d == FACE_E) {
                    // slice x = s: u = z, v = y
                    if (!any_x[s])
                        continue;
                    for (int z = 0; z < 16; z++) {
                        uint32_t bits = fbd[z * 16 + s];
                        int ci = LI(s, 0, z);
                        while (bits) {
                            int y = __builtin_ctz(bits);
                            bits &= bits - 1;
                            int i = ci + y;
                            int id = loc_id[i];
                            if (loc_id[i + no] == id)
                                continue;
                            mask[y * 16 + z] = KEY(blocks[id].tex[d], MESH_LIGHT_KEY(loc_li[i + no]), 0);
                            anyface = true;
                        }
                    }
                } else if (d == FACE_N || d == FACE_S) {
                    // slice z = s: u = x, v = y
                    if (!any_z[s])
                        continue;
                    for (int x = 0; x < 16; x++) {
                        uint32_t bits = fbd[s * 16 + x];
                        int ci = LI(x, 0, s);
                        while (bits) {
                            int y = __builtin_ctz(bits);
                            bits &= bits - 1;
                            int i = ci + y;
                            int id = loc_id[i];
                            if (loc_id[i + no] == id)
                                continue;
                            mask[y * 16 + x] = KEY(blocks[id].tex[d], MESH_LIGHT_KEY(loc_li[i + no]), 0);
                            anyface = true;
                        }
                    }
                } else {
                    // slice y = s: u = x, v = z
                    if (!(any_all & (1u << s)))
                        continue;
                    uint32_t bit = 1u << s;
                    for (int c = 0; c < 256; c++) {
                        if (!(fbd[c] & bit))
                            continue;
                        int x = c & 15, z = c >> 4;
                        int i = LI(x, s, z);
                        int id = loc_id[i];
                        if (cube_tab[id]) {
                            if (loc_id[i + no] == id)
                                continue;
                            mask[c] = KEY(blocks[id].tex[d], MESH_LIGHT_KEY(loc_li[i + no]), 0);
                        } else {
                            mask[c] = KEY(TEX_LAVA, loc_li[i] | 0x0F, liquid_height16(id));
                        }
                        anyface = true;
                    }
                }
                if (anyface)
                    greedy_slice(d, s, plane_off);
            }
        }
        MPROF(3);
        sort_quads();
        MPROF(4);
        int split = 0;
        while (split < nquads && !(quads[split].key & KEY_DARK))
            split++;
        gxl_init(&gl, scratch + pos, SCRATCH_WORDS - pos);
        emit_texgen_quads_range(&gl, d, 0, split);
        sec->off[d] = pos;
        sec->len[d] = gl.pos;
        sec->nq[d] = split;
        pos += gl.pos;
        gxl_init(&gl, scratch + pos, SCRATCH_WORDS - pos);
        emit_texgen_quads_range(&gl, d, split, nquads);
        sec->off[SL_DARK_W + d] = pos;
        sec->len[SL_DARK_W + d] = gl.pos;
        sec->nq[SL_DARK_W + d] = nquads - split;
        pos += gl.pos;
        total_quads += nquads;
    }

    MPROF(5);
    // ---- Water top (greedy, texgen) ----
    nquads = 0;
    {
        uint32_t any_all = 0;
        for (int z = 0; z < 16; z++)
            for (int x = 0; x < 16; x++) {
                int r = RI(x, z);
                uint32_t sp = (rspec[r] & INTERIOR_BITS) >> 1, fb = 0;
                if (sp) {
                    const uint8_t *col = &loc_id[LI(x, 0, z)];
                    while (sp) {
                        int y = __builtin_ctz(sp);
                        sp &= sp - 1;
                        int id = col[y], nid = col[y + 1];
                        if (is_water(id) && !is_water(nid) && !opaque_tab[nid])
                            fb |= 1u << y;
                    }
                }
                fbd[z * 16 + x] = fb;
                any_all |= fb;
            }
        for (int s = 0; s < 16 && any_all; s++) {
            uint32_t bit = 1u << s;
            if (!(any_all & bit))
                continue;
            for (int c = 0; c < 256; c++) {
                if (!(fbd[c] & bit))
                    continue;
                int i = LI(c & 15, s, c >> 4);
                int id = loc_id[i];
                mask[c] = KEY(TEX_WATER, loc_li[i], liquid_height16(id));
            }
            greedy_slice(FACE_U, s, 1);
        }
    }
    gxl_init(&gl, scratch + pos, SCRATCH_WORDS - pos);
    emit_texgen_quads(&gl, FACE_U);
    sec->off[SL_WATER_TOP] = pos;
    sec->len[SL_WATER_TOP] = gl.pos;
    sec->nq[SL_WATER_TOP] = nquads;
    pos += gl.pos;
    total_quads += nquads;
    int special_start_quads = total_quads;

    MPROF(6);
    // ---- Special shapes & liquid sides ----
    SpecState st = { 0 };
    gxl_init(&gl, scratch + pos, SCRATCH_WORDS - pos);
    st.l = &gl;
    st.tex = -1;
    st.mat = -1;
    SpecState stw = { 0 };
    static uint32_t water_scratch[6000];
    GxList glw;
    gxl_init(&glw, water_scratch, 6000);
    stw.l = &glw;
    stw.tex = -1;
    stw.mat = -1;

    for (int z = 0; z < 16; z++) {
        for (int x = 0; x < 16; x++) {
            uint32_t spbits = (rspec[RI(x, z)] & INTERIOR_BITS) >> 1;
            while (spbits) {
                int y = __builtin_ctz(spbits);
                spbits &= spbits - 1;
                int i = LI(x, y, z);
                int id = loc_id[i];
                const BlockDef *b = &blocks[id];
                int shape = b->shape;
                if (shape == SHAPE_NONE || shape == SHAPE_CUBE)
                    continue;
                int bx = x * 16, by = y * 16, bz = z * 16;
                int light = loc_li[i];
                switch (shape) {
                    case SHAPE_CROSS:
                        sp_cross(&st, b->tex[0], bx, by, bz, light, 2, 16);
                        total_quads += 2;
                        break;
                    case SHAPE_CROPS:
                        sp_plane_x(&st, b->tex[0], bx + 4, by - 1, bz, light);
                        sp_plane_x(&st, b->tex[0], bx + 12, by - 1, bz, light);
                        sp_plane_z(&st, b->tex[0], bz + 4, bx, by - 1, light);
                        sp_plane_z(&st, b->tex[0], bz + 12, bx, by - 1, light);
                        total_quads += 4;
                        break;
                    case SHAPE_FIRE:
                        sp_cross(&st, TEX_FIRE, bx, by, bz, 0xFF, 0, 16);
                        sp_plane_x(&st, TEX_FIRE, bx + 1, by, bz, 0xFF);
                        sp_plane_x(&st, TEX_FIRE, bx + 15, by, bz, 0xFF);
                        total_quads += 4;
                        break;
                    case SHAPE_TORCH:
                        sp_torch(&st, id, bx, by, bz, light | 0x0E);
                        total_quads += 2;
                        break;
                    case SHAPE_LADDER: {
                        int f = id - B_LADDER;   // facing away from wall
                        sp_tex(&st, TEX_LADDER);
                        sp_mat(&st, FACE_N, light);
                        int tc[4][2] = { { 0, 16 }, { 16, 16 }, { 16, 0 }, { 0, 0 } };
                        int v[4][3];
                        if (f == 0) { // faces north, wall at south: plane z=15
                            int vv[4][3] = { { bx + 16, by, bz + 15 }, { bx, by, bz + 15 }, { bx, by + 16, bz + 15 }, { bx + 16, by + 16, bz + 15 } };
                            memcpy(v, vv, sizeof(v));
                        } else if (f == 2) {
                            int vv[4][3] = { { bx, by, bz + 1 }, { bx + 16, by, bz + 1 }, { bx + 16, by + 16, bz + 1 }, { bx, by + 16, bz + 1 } };
                            memcpy(v, vv, sizeof(v));
                        } else if (f == 1) { // faces east, wall at west: plane x=1
                            int vv[4][3] = { { bx + 1, by, bz + 16 }, { bx + 1, by, bz }, { bx + 1, by + 16, bz }, { bx + 1, by + 16, bz + 16 } };
                            memcpy(v, vv, sizeof(v));
                        } else {
                            int vv[4][3] = { { bx + 15, by, bz }, { bx + 15, by, bz + 16 }, { bx + 15, by + 16, bz + 16 }, { bx + 15, by + 16, bz } };
                            memcpy(v, vv, sizeof(v));
                        }
                        sp_quad(&st, v, tc);
                        total_quads++;
                        break;
                    }
                    case SHAPE_CACTUS: {
                        uint8_t skip = 0;
                        if (loc_id[LI(x, y + 1, z)] == B_CACTUS) skip |= 1 << FACE_U;
                        if (loc_id[LI(x, y - 1, z)] == B_CACTUS) skip |= 1 << FACE_D;
                        sp_box(&st, b, bx + 1, by, bz + 1, bx + 15, by + 16, bz + 15, light, skip, 0);
                        total_quads += 6;
                        break;
                    }
                    case SHAPE_FARMLAND: {
                        uint8_t skip = 0;
                        for (int f = 0; f < 6; f++) {
                            if (f == FACE_U)
                                continue;
                            if (block_opaque(loc_id[LI(x + face_dx[f], y + face_dy[f], z + face_dz[f])]))
                                skip |= 1 << f;
                        }
                        int up_light = loc_li[LI(x, y + 1, z)];
                        // top uses light above, sides use own (dark) -> use above for all
                        sp_box(&st, b, bx, by, bz, bx + 16, by + 15, bz + 16, up_light, skip, 0);
                        total_quads += 3;
                        break;
                    }
                    case SHAPE_BED: {
                        int idx = id - B_BED;
                        int f = idx & 3;          // direction foot -> head
                        // texture "up" points to head: rotation per facing
                        static const uint8_t rot[4] = { 0, 1, 2, 3 };
                        sp_box(&st, b, bx, by, bz, bx + 16, by + 9, bz + 16, light, 0, rot[f]);
                        total_quads += 6;
                        break;
                    }
                    case SHAPE_DOOR: {
                        int idx = id - B_DOOR;
                        int f = idx & 3, open = (idx >> 2) & 1;
                        int side = open ? ((f + 1) & 3) : ((f + 2) & 3);   // closed: edge nearest the placer
                        int x0 = bx, x1 = bx + 16, z0 = bz, z1 = bz + 16;
                        switch (side) {
                            case 0: z1 = bz + 3; break;      // north edge
                            case 1: x0 = bx + 13; break;     // east edge
                            case 2: z0 = bz + 13; break;     // south edge
                            default: x1 = bx + 3; break;     // west edge
                        }
                        sp_box(&st, b, x0, by, z0, x1, by + 16, z1, light, 0, 0);
                        total_quads += 6;
                        break;
                    }
                    case SHAPE_LIQUID: {
                        SpecState *s2 = is_water(id) ? &stw : &st;
                        int h = liquid_height16(id);
                        int above = loc_id[LI(x, y + 1, z)];
                        bool same_above = is_water(id) ? is_water(above) : is_lava(above);
                        if (same_above)
                            h = 16;
                        for (int f = 0; f < 6; f++) {
                            if (f == FACE_U)
                                continue; // top handled by greedy lists
                            int ni = LI(x + face_dx[f], y + face_dy[f], z + face_dz[f]);
                            int nid = loc_id[ni];
                            if (block_opaque(nid))
                                continue;
                            if (is_water(id) ? is_water(nid) : is_lava(nid))
                                continue;
                            if (f == FACE_D && block_solid(nid))
                                continue;
                            emit_liquid_side(s2, id, f, bx, by, bz, h, is_lava(id) ? (loc_li[ni] | 0x0F) : loc_li[ni]);
                            total_quads++;
                        }
                        break;
                    }
                    default:
                        break;
                }
            }
        }
    }
    if (st.in_begin)
        gxl_cmd0(&gl, GXC_END);
    gxl_flush(&gl);
    sec->off[SL_SPECIAL] = pos;
    sec->len[SL_SPECIAL] = gl.pos;
    sec->nq[SL_SPECIAL] = total_quads - special_start_quads;
    pos += gl.pos;

    if (stw.in_begin)
        gxl_cmd0(&glw, GXC_END);
    gxl_flush(&glw);
    if (glw.pos && pos + glw.pos <= SCRATCH_WORDS) {
        memcpy(scratch + pos, water_scratch, glw.pos * 4);
        sec->off[SL_WATER_OTHER] = pos;
        sec->len[SL_WATER_OTHER] = glw.pos;
        sec->nq[SL_WATER_OTHER] = glw.pos / 6;
        pos += glw.pos;
    }

    MPROF(7);
    sec->quads = total_quads;
    if (pos > 0) {
        uint32_t need = pos * 4;
        while (mesh_bytes + need > mesh_budget && mesh_evict_one(sec))
            ;
        sec->list = malloc(need);
        while (!sec->list && mesh_evict_one(sec))
            sec->list = malloc(need);
        if (sec->list) {
            memcpy(sec->list, scratch, need);
            DC_FlushRange(sec->list, need);
            sec->words = pos;
            mesh_bytes += need;
        } else {
            memset(sec->len, 0, sizeof(sec->len));
            memset(sec->nq, 0, sizeof(sec->nq));
            sec->built = 0;   // retry later
            sec->dirty = 1;
        }
    }
}
