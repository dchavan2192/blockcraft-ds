// Villages: generation, villager records, professions and trading
#pragma once
#include "common.h"
#include "items.h"

#define MAX_VILLAGES 8
#define VILLAGE_MAX_VILLAGERS 8
#define TRADES_PER_PROF 10
#define VILLAGER_MAX_LEVEL 5

enum {
    PROF_FARMER = 0, PROF_LIBRARIAN, PROF_ARMORER, PROF_TOOLSMITH, PROF_WEAPONSMITH,
    PROF_BUTCHER, PROF_SHEPHERD, PROF_FLETCHER, PROF_MASON, PROF_CLERIC,
    PROF_COUNT
};

// Headwear drawn on top of the robe
enum { HAT_NONE = 0, HAT_BRIM, HAT_CAP, HAT_HOOD };

typedef struct {
    const char *name;
    uint16_t robe;       // RGB15 tint for the robe
    uint16_t accent;     // RGB15 tint for hat or apron
    uint8_t hat;         // HAT_*
    uint8_t apron;       // draws an apron in the accent color
} ProfInfo;

// A trade: the player gives buy (+ buy2) and receives sell
typedef struct {
    uint16_t buy, buy2, sell;
    uint8_t buy_n, buy2_n, sell_n;
    uint8_t max_uses;    // uses before the villager needs to restock
    uint8_t xp;          // villager experience per trade
    uint8_t level;       // villager level that unlocks it (1..5)
} Trade;

typedef struct {
    uint8_t alive;
    uint8_t loaded;            // has a live entity right now
    uint8_t prof, level;
    uint16_t xp;
    uint8_t name;
    uint8_t health;
    uint8_t uses[TRADES_PER_PROF];
    int16_t x, z;              // last known position (blocks)
    int8_t y;
    int8_t bed_y;
    int16_t bed_x, bed_z;      // bed head block
    int16_t stand_x, stand_z;  // floor cell beside the bed, inside the house
    int8_t stand_y;
    int8_t door_f;             // door facing out of the house (0..3 = N,E,S,W)
    int16_t door_x, door_z;    // lower door block
    int8_t door_y;
    int8_t work_y;
    int16_t work_x, work_z;    // job site (farm, furnace, bookshelf...)
} VillagerRec;

typedef struct {
    uint8_t used;
    uint8_t style;             // VS_* building palette
    uint8_t nvillagers;
    uint8_t golem_alive;
    uint8_t golem_loaded;
    uint8_t golem_health;
    uint8_t golem_days;        // days since the golem died
    uint8_t pad;
    int16_t x, z;              // well (center)
    int8_t y;
    int8_t golem_y;
    int16_t golem_x, golem_z;
    int16_t farm_x, farm_z;    // farm center (for farmers)
    int8_t farm_y;
    int8_t pad2[3];
    VillagerRec v[VILLAGE_MAX_VILLAGERS];
} Village;

extern Village villages[MAX_VILLAGES];
extern const ProfInfo prof_info[PROF_COUNT];

void villages_clear(void);
// World generation: builds villages into the raw block array. mask (size*size)
// gets 1 on columns that decoration (trees, flowers) must leave alone.
// Returns the number of villages made.
int villages_generate(uint32_t seed, uint8_t *mask);

// Per game tick: brings villagers to life near the player and back to records
// far away, restocks trades and lets new villagers move in.
void villages_tick(void);
// Copy live villagers and golems into their records (before saving)
void villages_sync(void);
// Entities were all removed (new world, load): nothing is loaded any more
void villages_unloaded(void);

VillagerRec *village_rec(int vil, int slot);
Village *village_get(int vil);
const char *villager_name(const VillagerRec *r);
const char *villager_level_name(int level);
int villager_xp_for_level(int level);      // xp needed to reach level (level+1)

// Trades a villager offers right now (unlocked by level). Fills idx with
// indexes into the profession's trade table and returns how many.
int villager_offers(const VillagerRec *r, uint8_t *idx);
const Trade *villager_trade_def(const VillagerRec *r, int t);
// How many times the player could make trade t right now (stock and items)
int villager_trade_times(const VillagerRec *r, int t);
// Make trade t up to `times` times. Returns trades made.
int villager_do_trade(VillagerRec *r, int t, int times);

// Doors (shared with villager AI)
bool village_door_is_open(int x, int y, int z);
void village_door_set(int x, int y, int z, bool open);
