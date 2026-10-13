// Item definitions
#pragma once
#include "common.h"

enum { IK_NONE = 0, IK_BLOCK, IK_TOOL, IK_FOOD, IK_ARMOR, IK_MISC, IK_BUCKET, IK_PLACER };

// Item ids. 1..255 are block items (same id as the block).
enum {
    I_NONE = 0,
    I_FIRST_ITEM = 256,
    I_STICK = 256, I_COAL, I_CHARCOAL, I_IRON_INGOT, I_GOLD_INGOT, I_DIAMOND, I_REDSTONE, I_FLINT,
    I_STRING, I_FEATHER, I_GUNPOWDER, I_BONE, I_BONEMEAL, I_ROTTEN_FLESH, I_LEATHER, I_WHEAT, I_SEEDS,
    I_BREAD, I_APPLE, I_RAW_PORK, I_COOKED_PORK, I_RAW_BEEF, I_STEAK, I_RAW_CHICKEN, I_COOKED_CHICKEN,
    I_MUSHROOM_STEW, I_BOWL, I_EGG, I_BUCKET, I_WATER_BUCKET, I_LAVA_BUCKET, I_MILK_BUCKET,
    I_BOW, I_ARROW, I_FLINT_STEEL, I_SHEARS, I_BRICK, I_CLAY_BALL, I_PAPER, I_SUGAR, I_PUMPKIN_PIE,
    I_DOOR, I_BED,
    I_TOOLS_START,
    // 5 materials x 5 tool kinds: pickaxe, axe, shovel, sword, hoe
    I_TOOLS_END = I_TOOLS_START + 25,
    I_ARMOR_START = I_TOOLS_END,
    // 4 materials (leather, iron, gold, diamond) x 4 slots (helmet, chest, legs, boots)
    I_ARMOR_END = I_ARMOR_START + 16,
    I_COUNT = I_ARMOR_END
};

enum { MAT_WOOD = 0, MAT_STONE, MAT_IRON, MAT_GOLD, MAT_DIAMOND };
enum { TK_PICKAXE = 0, TK_AXE, TK_SHOVEL, TK_SWORD, TK_HOE };
#define TOOL_ID(mat, kind) (I_TOOLS_START + (mat) * 5 + (kind))
enum { AM_LEATHER = 0, AM_IRON, AM_GOLD, AM_DIAMOND };
enum { AS_HEAD = 0, AS_CHEST, AS_LEGS, AS_FEET };
#define ARMOR_ID(mat, slot) (I_ARMOR_START + (mat) * 4 + (slot))

typedef struct {
    const char *name;
    uint16_t icon;
    uint8_t max_stack;
    uint8_t kind;
    uint8_t tool_type;   // TOOL_* from blocks.h
    uint8_t tool_tier;   // harvest tier: 1 wood/gold, 2 stone, 3 iron, 4 diamond
    uint8_t speed;       // mining speed multiplier
    uint8_t damage;      // attack damage in half-hearts
    uint16_t durability;
    uint8_t food;        // hunger points restored
    uint8_t saturation;  // saturation x10
    uint8_t armor_slot;
    uint8_t armor_points;
    uint16_t places;     // block placed (0 = none)
    uint16_t fuel;       // furnace burn time in ticks
    uint16_t smelt;      // smelting result
} ItemDef;

extern ItemDef items[I_COUNT];

void items_init(void);

static inline const ItemDef *itemdef(int id) { return &items[id]; }
static inline bool item_valid(int id) { return id > 0 && id < I_COUNT && items[id].name != NULL; }

typedef struct {
    uint16_t id;
    uint8_t count;
    uint8_t pad;
    uint16_t damage;     // tool wear
} ItemStack;

static inline bool stack_empty(const ItemStack *s) { return s->id == 0 || s->count == 0; }
static inline void stack_clear(ItemStack *s) { s->id = 0; s->count = 0; s->damage = 0; }
