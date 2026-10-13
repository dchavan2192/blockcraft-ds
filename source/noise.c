// Fixed-point periodic gradient (Perlin-style) noise.
#include "noise.h"

static const int8_t grad2[8][2] = {
    { 1, 1 }, { -1, 1 }, { 1, -1 }, { -1, -1 }, { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
};

static const int8_t grad3[16][3] = {
    { 1, 1, 0 }, { -1, 1, 0 }, { 1, -1, 0 }, { -1, -1, 0 },
    { 1, 0, 1 }, { -1, 0, 1 }, { 1, 0, -1 }, { -1, 0, -1 },
    { 0, 1, 1 }, { 0, -1, 1 }, { 0, 1, -1 }, { 0, -1, -1 },
    { 1, 1, 0 }, { -1, 1, 0 }, { 0, -1, 1 }, { 0, -1, -1 },
};

void noise_seed(Noise *n, uint32_t seed)
{
    for (int i = 0; i < 256; i++)
        n->perm[i] = i;
    uint32_t s = seed * 2654435761u + 12345;
    for (int i = 255; i > 0; i--) {
        s = s * 1664525u + 1013904223u;
        int j = (s >> 8) % (i + 1);
        uint8_t t = n->perm[i];
        n->perm[i] = n->perm[j];
        n->perm[j] = t;
    }
    for (int i = 0; i < 256; i++)
        n->perm[256 + i] = n->perm[i];
}

// Quintic fade on 0..4096
ARM_CODE static inline int32_t fade(int32_t t)
{
    // 6t^5 - 15t^4 + 10t^3
    int32_t t2 = (t * t) >> 12;
    int32_t t3 = (t2 * t) >> 12;
    int32_t inner = ((t * (t * 6 - 15 * 4096)) >> 12) + 10 * 4096; // 6t^2 - 15t + 10
    return (int32_t)(((int64_t)t3 * inner) >> 12);
}

ARM_CODE static inline int32_t lerp(int32_t a, int32_t b, int32_t t)
{
    return a + (((b - a) * t) >> 12);
}

// periods are powers of two
static inline int wrapi(int v, int p)
{
    return v & (p - 1);
}

ARM_CODE int32_t noise2(const Noise *n, int x, int z, int cell_shift, int period)
{
    int cell = 1 << cell_shift;
    int xi = x >> cell_shift, zi = z >> cell_shift;
    int32_t fx = ((x & (cell - 1)) << 12) >> cell_shift;
    int32_t fz = ((z & (cell - 1)) << 12) >> cell_shift;
    int x0 = wrapi(xi, period), x1 = wrapi(xi + 1, period);
    int z0 = wrapi(zi, period), z1 = wrapi(zi + 1, period);
    const uint8_t *p = n->perm;
    int g00 = p[p[x0 & 255] + (z0 & 255)] & 7;
    int g10 = p[p[x1 & 255] + (z0 & 255)] & 7;
    int g01 = p[p[x0 & 255] + (z1 & 255)] & 7;
    int g11 = p[p[x1 & 255] + (z1 & 255)] & 7;
    int32_t d00 = grad2[g00][0] * fx + grad2[g00][1] * fz;
    int32_t d10 = grad2[g10][0] * (fx - 4096) + grad2[g10][1] * fz;
    int32_t d01 = grad2[g01][0] * fx + grad2[g01][1] * (fz - 4096);
    int32_t d11 = grad2[g11][0] * (fx - 4096) + grad2[g11][1] * (fz - 4096);
    int32_t u = fade(fx), v = fade(fz);
    int32_t r = lerp(lerp(d00, d10, u), lerp(d01, d11, u), v);
    return r; // roughly -4096..4096
}

ARM_CODE int32_t noise3(const Noise *n, int x, int y, int z, int cell_shift, int period_xz)
{
    int cell = 1 << cell_shift;
    int xi = x >> cell_shift, yi = y >> cell_shift, zi = z >> cell_shift;
    int32_t fx = ((x & (cell - 1)) << 12) >> cell_shift;
    int32_t fy = ((y & (cell - 1)) << 12) >> cell_shift;
    int32_t fz = ((z & (cell - 1)) << 12) >> cell_shift;
    int x0 = wrapi(xi, period_xz), x1 = wrapi(xi + 1, period_xz);
    int z0 = wrapi(zi, period_xz), z1 = wrapi(zi + 1, period_xz);
    int y0 = yi & 255, y1 = (yi + 1) & 255;
    const uint8_t *p = n->perm;
    int32_t d[8];
    for (int c = 0; c < 8; c++) {
        int cx = (c & 1) ? x1 : x0;
        int cy = (c & 2) ? y1 : y0;
        int cz = (c & 4) ? z1 : z0;
        int g = p[p[p[cx & 255] + cy] + (cz & 255)] & 15;
        int32_t dx = (c & 1) ? fx - 4096 : fx;
        int32_t dy = (c & 2) ? fy - 4096 : fy;
        int32_t dz = (c & 4) ? fz - 4096 : fz;
        d[c] = grad3[g][0] * dx + grad3[g][1] * dy + grad3[g][2] * dz;
    }
    int32_t u = fade(fx), v = fade(fy), w = fade(fz);
    int32_t a = lerp(lerp(d[0], d[1], u), lerp(d[2], d[3], u), v);
    int32_t b = lerp(lerp(d[4], d[5], u), lerp(d[6], d[7], u), v);
    return lerp(a, b, w);
}

ARM_CODE int32_t fbm2(const Noise *n, int x, int z, int cell_shift, int octaves, int world_size)
{
    // 1/total for totals 4096, 6144, 7168, 7680 in 4.12 relative to 4096
    static const int32_t inv_total[5] = { 4096, 4096, 2731, 2341, 2185 };
    int32_t sum = 0;
    int amp = 4096;
    int used = 0;
    for (int o = 0; o < octaves; o++) {
        int cs = cell_shift - o;
        if (cs < 0)
            break;
        int period = world_size >> cs;
        sum += (noise2(n, x + o * 31, z + o * 17, cs, period) * amp) >> 12;
        amp >>= 1;
        used++;
    }
    return (sum * inv_total[used]) >> 12;
}
