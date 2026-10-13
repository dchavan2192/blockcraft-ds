// Block definitions
#pragma once
#include "common.h"

enum {
    SHAPE_NONE = 0,
    SHAPE_CUBE,
    SHAPE_CROSS,
    SHAPE_TORCH,
    SHAPE_LIQUID,
    SHAPE_LADDER,
    SHAPE_DOOR,
    SHAPE_BED,
    SHAPE_CACTUS,
    SHAPE_FARMLAND,
    SHAPE_FIRE,
    SHAPE_CROPS,
};

enum {
    RENDER_NONE = 0,
    RENDER_OPAQUE,
    RENDER_CUTOUT,
    RENDER_TRANSLUCENT,
};

// Flags
#define BF_SOLID       0x0001  // collides with entities
#define BF_OPAQUE      0x0002  // full opaque cube: hides neighbor faces
#define BF_REPLACEABLE 0x0004  // can be replaced by placing a block (air, water, tallgrass)
#define BF_GRAVITY     0x0008  // falls (sand, gravel)
#define BF_CLIMBABLE   0x0010
#define BF_LIQUID      0x0020
#define BF_NEEDS_SUPPORT 0x0040 // breaks when block below removed (plants, torches)
#define BF_INTERACT    0x0080  // has a right-click action
#define BF_FLAMMABLE   0x0100
#define BF_SELECTABLE  0x0200  // can be targeted by the cursor
#define BF_HURTS       0x0400  // cactus / lava / fire damage on contact

enum { TOOL_NONE = 0, TOOL_PICKAXE, TOOL_AXE, TOOL_SHOVEL, TOOL_SWORD, TOOL_HOE, TOOL_SHEARS };
enum { SND_STONE = 0, SND_WOOD, SND_GRASS, SND_SAND, SND_GLASS, SND_CLOTH, SND_NONE };

typedef struct {
    const char *name;
    uint8_t tex[6];
    uint8_t shape;
    uint8_t render;
    uint16_t flags;
    uint8_t opacity;     // light attenuation (15 = blocks)
    uint8_t emit;        // light emission
    uint16_t hardness;   // x10, 0xFFFF = unbreakable
    uint8_t tool;
    uint8_t tier;        // minimum tool tier to get drops (0 = hand)
    uint8_t sound;
    uint8_t base;        // base block id for state variants (same id if none)
    uint16_t drop;       // item id dropped (0 = nothing)
    uint8_t drop_count;
} BlockDef;

// Block ids
enum {
    B_AIR = 0,
    B_STONE, B_GRASS, B_DIRT, B_COBBLESTONE,
    B_PLANKS_OAK, B_PLANKS_BIRCH, B_PLANKS_SPRUCE,
    B_SAPLING_OAK, B_SAPLING_BIRCH, B_SAPLING_SPRUCE,
    B_BEDROCK,
    B_WATER, B_WATER_F1, B_WATER_F2, B_WATER_F3, B_WATER_F4, B_WATER_F5, B_WATER_F6, B_WATER_F7, B_WATER_FALL,
    B_LAVA, B_LAVA_F1, B_LAVA_F2, B_LAVA_F3, B_LAVA_FALL,
    B_SAND, B_GRAVEL,
    B_GOLD_ORE, B_IRON_ORE, B_COAL_ORE, B_DIAMOND_ORE, B_REDSTONE_ORE,
    B_LOG_OAK, B_LOG_BIRCH, B_LOG_SPRUCE,
    B_LEAVES_OAK, B_LEAVES_BIRCH, B_LEAVES_SPRUCE,
    B_GLASS, B_SANDSTONE,
    B_WOOL_WHITE, B_WOOL_LAST = B_WOOL_WHITE + 15,
    B_FLOWER_RED, B_FLOWER_YELLOW, B_MUSHROOM_BROWN, B_MUSHROOM_RED, B_TALLGRASS, B_DEADBUSH,
    B_GOLD_BLOCK, B_IRON_BLOCK, B_DIAMOND_BLOCK, B_COAL_BLOCK,
    B_BRICKS, B_TNT, B_BOOKSHELF, B_MOSSY_COBBLESTONE, B_OBSIDIAN, B_STONEBRICKS,
    B_TORCH, B_TORCH_N, B_TORCH_E, B_TORCH_S, B_TORCH_W,   // wall torches: attached to block on that side
    B_FIRE,
    B_CHEST, B_CHEST_LAST = B_CHEST + 3,
    B_CRAFTING_TABLE,
    B_WHEAT, B_WHEAT_LAST = B_WHEAT + 7,
    B_FARMLAND, B_FARMLAND_WET,
    B_FURNACE, B_FURNACE_LAST = B_FURNACE + 3,
    B_FURNACE_LIT, B_FURNACE_LIT_LAST = B_FURNACE_LIT + 3,
    B_LADDER, B_LADDER_LAST = B_LADDER + 3,
    B_SNOWY_GRASS, B_SNOW, B_ICE, B_CACTUS, B_CLAY,
    B_PUMPKIN, B_PUMPKIN_LAST = B_PUMPKIN + 3,
    B_JACKOLANTERN, B_JACKOLANTERN_LAST = B_JACKOLANTERN + 3,
    B_DOOR, B_DOOR_LAST = B_DOOR + 15,     // +facing(0..3) +open*4 +top*8
    B_BED, B_BED_LAST = B_BED + 7,         // +facing(0..3) +head*4
    B_COUNT
};

extern BlockDef blocks[256];

void blocks_init(void);

static inline const BlockDef *blockdef(int id) { return &blocks[id]; }
static inline bool block_solid(int id) { return (blocks[id].flags & BF_SOLID) != 0; }
static inline bool block_opaque(int id) { return (blocks[id].flags & BF_OPAQUE) != 0; }
static inline bool block_liquid(int id) { return (blocks[id].flags & BF_LIQUID) != 0; }
static inline bool block_replaceable(int id) { return (blocks[id].flags & BF_REPLACEABLE) != 0; }
static inline bool is_water(int id) { return id >= B_WATER && id <= B_WATER_FALL; }
static inline bool is_lava(int id) { return id >= B_LAVA && id <= B_LAVA_FALL; }
static inline bool is_wall_torch(int id) { return id >= B_TORCH_N && id <= B_TORCH_W; }
static inline bool is_door(int id) { return id >= B_DOOR && id <= B_DOOR_LAST; }
static inline bool is_bed(int id) { return id >= B_BED && id <= B_BED_LAST; }
static inline bool is_chest(int id) { return id >= B_CHEST && id <= B_CHEST_LAST; }
static inline bool is_furnace(int id) { return (id >= B_FURNACE && id <= B_FURNACE_LAST) || (id >= B_FURNACE_LIT && id <= B_FURNACE_LIT_LAST); }
static inline bool is_wheat(int id) { return id >= B_WHEAT && id <= B_WHEAT_LAST; }
static inline bool is_leaves(int id) { return id >= B_LEAVES_OAK && id <= B_LEAVES_SPRUCE; }
static inline bool is_log(int id) { return id >= B_LOG_OAK && id <= B_LOG_SPRUCE; }
static inline bool is_ladder(int id) { return id >= B_LADDER && id <= B_LADDER_LAST; }

// Liquid level: 0 = source, 1..7 = flowing distance, 8 = falling
int liquid_level(int id);
// Liquid surface height in 1/16 block units (0..16)
int liquid_height16(int id);
