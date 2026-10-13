// Fixed-point periodic gradient noise
#pragma once
#include "common.h"

typedef struct {
    uint8_t perm[512];
} Noise;

void noise_seed(Noise *n, uint32_t seed);

// 2D noise. x, z in blocks; cell = lattice size in blocks (power of 2);
// period = repeat length in cells. Returns about -4096..4096.
int32_t noise2(const Noise *n, int x, int z, int cell_shift, int period);

// 3D noise at integer lattice coordinates scaled by cell size.
int32_t noise3(const Noise *n, int x, int y, int z, int cell_shift, int period_xz);

// Sum of octaves; first octave cell = 1 << cell_shift
int32_t fbm2(const Noise *n, int x, int z, int cell_shift, int octaves, int world_size);
