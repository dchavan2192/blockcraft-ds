// Low level geometry command helpers (packed display lists + immediate FIFO)
#pragma once
#include "common.h"

#define GXC_MTX_MODE   0x10
#define GXC_MTX_PUSH   0x11
#define GXC_MTX_POP    0x12
#define GXC_MTX_IDENT  0x15
#define GXC_MTX_LOAD43 0x17
#define GXC_MTX_MULT43 0x19
#define GXC_MTX_SCALE  0x1B
#define GXC_MTX_TRANS  0x1C
#define GXC_COLOR      0x20
#define GXC_NORMAL     0x21
#define GXC_TEXCOORD   0x22
#define GXC_VTX16      0x23
#define GXC_VTX10      0x24
#define GXC_POLY_ATTR  0x29
#define GXC_TEXIMAGE   0x2A
#define GXC_PLTT_BASE  0x2B
#define GXC_DIF_AMB    0x30
#define GXC_SPE_EMI    0x31
#define GXC_BEGIN      0x40
#define GXC_END        0x41

#define GX_V10(x, y, z) ((uint32_t)(((x) & 0x3FF) | (((y) & 0x3FF) << 10) | (((z) & 0x3FF) << 20)))
#define GX_TC(s, t) ((uint32_t)(((s) & 0xFFFF) | ((uint32_t)((t) & 0xFFFF) << 16)))

// Texture parameter for a 16x16 256-color texture at slot index (256 bytes each)
#define TEXGEN_NONE 0
#define TEXGEN_VERTEX 3
static inline uint32_t gx_texparam16(int slot, int texgen)
{
    uint32_t addr = (uint32_t)slot * 256;
    return (addr >> 3) | (1 << 16) | (1 << 17) | (1 << 20) | (1 << 23) | (4 << 26) | (1u << 29) | ((uint32_t)texgen << 30);
}

typedef struct {
    uint32_t *buf;
    int pos;
    int cap;
    uint8_t cmd[4];
    uint32_t par[8];
    int nc, np;
    bool overflow;
} GxList;

static inline void gxl_init(GxList *l, uint32_t *buf, int cap)
{
    l->buf = buf;
    l->pos = 0;
    l->cap = cap;
    l->nc = l->np = 0;
    l->overflow = false;
}

static inline void gxl_flush(GxList *l)
{
    if (!l->nc)
        return;
    if (l->pos + 1 + l->np > l->cap) {
        l->overflow = true;
        l->nc = l->np = 0;
        return;
    }
    uint32_t h = 0;
    for (int i = 0; i < l->nc; i++)
        h |= (uint32_t)l->cmd[i] << (i * 8);
    l->buf[l->pos++] = h;
    for (int i = 0; i < l->np; i++)
        l->buf[l->pos++] = l->par[i];
    l->nc = l->np = 0;
}

static inline void gxl_cmd0(GxList *l, uint8_t c)
{
    l->cmd[l->nc++] = c;
    if (l->nc == 4)
        gxl_flush(l);
}

static inline void gxl_cmd1(GxList *l, uint8_t c, uint32_t p)
{
    l->cmd[l->nc++] = c;
    l->par[l->np++] = p;
    if (l->nc == 4)
        gxl_flush(l);
}

// Immediate mode: send a list fragment via DMA
void gx_call(const uint32_t *words, int count);

