// Section meshing into hardware display lists
#pragma once
#include "common.h"

enum {
    SL_W = 0, SL_E, SL_D, SL_U, SL_N, SL_S,   // opaque directional (texgen)
    SL_SPECIAL,                               // cutout specials, explicit texcoords
    SL_WATER_TOP,                             // translucent, texgen (up)
    SL_WATER_OTHER,                           // translucent, explicit texcoords
    SL_DARK_W, SL_DARK_E, SL_DARK_D, SL_DARK_U, SL_DARK_N, SL_DARK_S,  // unlit faces (skippable far away)
    SL_COUNT
};

typedef struct {
    uint32_t *list;
    uint16_t off[SL_COUNT];
    uint16_t len[SL_COUNT];
    uint16_t nq[SL_COUNT];   // quads per sublist
    uint16_t vis;       // face connectivity bits
    uint16_t quads;
    uint16_t words;     // size of list in 32-bit words
    uint8_t dirty;
    uint8_t built;
    uint8_t empty;      // only air
    uint8_t solid;      // fully opaque, no geometry visible from inside
    uint8_t sky_exposed; // some open cell receives sky light
    uint32_t last_seen; // frame counter (visibility traversal)
    uint32_t last_drawn; // frame counter (kept by the polygon budget)
} Section;

extern Section *sections;
extern int sections_count;

bool mesh_init(void);
void mesh_shutdown(void);
static inline Section *section_at(int cx, int sy, int cz);
void mesh_build(int cx, int sy, int cz);
void mesh_mark_section(int cx, int sy, int cz);
void mesh_free_section(Section *s);
void mesh_invalidate_all(void);
// Display list memory accounting
extern uint32_t mesh_bytes, mesh_budget;
// Index helper
int section_index(int cx, int sy, int cz);

static inline bool vis_connected(uint16_t vis, int a, int b)
{
    if (a == b)
        return true;
    if (a > b) {
        int t = a; a = b; b = t;
    }
    // pair index for a<b among 6 faces
    static const uint8_t base[6] = { 0, 5, 9, 12, 14, 15 };
    return (vis >> (base[a] + (b - a - 1))) & 1;
}

#include "world.h"
static inline Section *section_at(int cx, int sy, int cz)
{
    cx &= world.chunks - 1;
    cz &= world.chunks - 1;
    return &sections[(cz * world.chunks + cx) * SEC_Y + sy];
}

// Lighting color tables used by the mesher and entity renderer
extern uint16_t light_ambient[6][16];
extern uint16_t light_emission[6][16];
void mesh_init_light_tables(void);
