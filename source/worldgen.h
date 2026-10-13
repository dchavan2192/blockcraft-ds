#pragma once
#include "common.h"

enum { BIOME_OCEAN = 0, BIOME_BEACH, BIOME_PLAINS, BIOME_FOREST, BIOME_DESERT, BIOME_SNOWY, BIOME_MOUNTAINS };

extern uint8_t *biome_map;

void worldgen_generate(uint32_t seed, void (*progress)(const char *stage, int pct));
void worldgen_grow_tree(int x, int y, int z, int kind);   // kind: 0 oak, 1 birch, 2 spruce
int worldgen_biome(int x, int z);
const char *biome_name(int b);
