// Inventories, crafting, chests and furnaces
#pragma once
#include "common.h"
#include "items.h"

#define INV_SIZE 36      // 0..8 hotbar, 9..35 main
#define CHEST_SIZE 27
#define MAX_CHESTS 96
#define MAX_FURNACES 48

// Adds items to an inventory array; returns the count that did not fit.
int inv_add(ItemStack *inv, int n, int id, int count, int damage);
int inv_count_item(const ItemStack *inv, int n, int id);
bool inv_remove_item(ItemStack *inv, int n, int id, int count);
int item_max_stack(int id);

// Crafting
// grid is gw x gw (2 or 3), result filled if a recipe matches
bool craft_match(const ItemStack *grid, int gw, ItemStack *result);
void craft_consume(ItemStack *grid, int gw);
void recipes_init(void);

// Recipe book (crafting directly from an inventory)
typedef struct {
    uint16_t out;
    uint8_t count;
    uint8_t needs_table;   // larger than 2x2
    uint8_t ning;          // distinct ingredients
    uint16_t ing[9];       // ingredient keys (item ids or groups)
    uint8_t need[9];       // amount of each per craft
} RecipeInfo;
int recipe_total(void);
void recipe_info(int r, RecipeInfo *ri);
int recipe_ing_display(uint16_t key);
const char *recipe_ing_name(uint16_t key);
int recipe_have(uint16_t key, const ItemStack *inv, int n);
int recipe_times(int r, const ItemStack *inv, int n);          // how many times it can be crafted now
int recipe_craft(int r, ItemStack *inv, int n, int times);     // returns crafts done

// Chests
typedef struct {
    int16_t x, z;
    int8_t y;
    uint8_t used;
    ItemStack items[CHEST_SIZE];
} Chest;

typedef struct {
    int16_t x, z;
    int8_t y;
    uint8_t used;
    ItemStack in, fuel, out;
    int16_t burn, burn_max;   // remaining fuel ticks / total of current fuel
    int16_t cook;             // cooking progress (0..200)
} Furnace;

extern Chest chests[MAX_CHESTS];
extern Furnace furnaces[MAX_FURNACES];

Chest *chest_get(int x, int y, int z, bool create);
Furnace *furnace_get(int x, int y, int z, bool create);
void chest_remove(int x, int y, int z);
void furnace_remove(int x, int y, int z);
void tile_entities_clear(void);
void furnaces_tick(void);
